// gate: R6 Seam 3 updates · spec: PLAN.md R6
//
// r6load: synthetic webpage documents, a rate-limited partial-update stream, and an open-loop query client.
//
//	r6load docs    -start S -n N            JSONL puts for ids [S, S+N) on stdout (deterministic per id)
//	r6load updates -rate R -dur D -ndocs N  JSONL assign updates on stdout at R ops/s (0 = as fast as stdout drains)
//	r6load query   -qps Q -dur D -out F     open-loop queries; one line per query: start_us,latency_us,status,hits
//	r6load qmax    -conc C -dur D           closed-loop queries, prints achieved QPS and p50/p99
package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"math"
	"math/rand"
	"net/http"
	"os"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
)

const vocabSize = 200000

var vocab = func() []string {
	r := rand.New(rand.NewSource(42))
	v := make([]string, vocabSize)
	seen := map[string]bool{}
	for i := range v {
		for {
			n := 3 + r.Intn(7)
			b := make([]byte, n)
			for j := range b {
				b[j] = byte('a' + r.Intn(26))
			}
			s := string(b)
			if !seen[s] {
				seen[s] = true
				v[i] = s
				break
			}
		}
	}
	return v
}()

var languages = []string{"en", "en", "en", "en", "de", "fr", "es", "it", "pt", "ja", "ru", "nl"}

type gen struct {
	r     *rand.Rand
	words *rand.Zipf
	doms  *rand.Zipf
}

func newGen(seed int64) *gen {
	r := rand.New(rand.NewSource(seed))
	return &gen{r: r, words: rand.NewZipf(r, 1.07, 2, vocabSize-1), doms: rand.NewZipf(r, 1.2, 4, 1999999)}
}

func (g *gen) text(n int) string {
	var b strings.Builder
	for i := 0; i < n; i++ {
		if i > 0 {
			b.WriteByte(' ')
		}
		b.WriteString(vocab[g.words.Uint64()])
	}
	return b.String()
}

func (g *gen) doc(id int64) map[string]any {
	r := g.r
	dom := g.doms.Uint64()
	host := fmt.Sprintf("%s%d.%s", vocab[dom%vocabSize], dom, []string{"com", "org", "net", "de", "io"}[dom%5])
	parts := strings.Split(host, ".")
	for i, j := 0, len(parts)-1; i < j; i, j = i+1, j-1 {
		parts[i], parts[j] = parts[j], parts[i]
	}
	depth := r.Intn(5)
	path := ""
	for i := 0; i < depth; i++ {
		path += "/" + vocab[g.words.Uint64()]
	}
	if path == "" {
		path = "/"
	}
	url := "https://" + host + path
	quality := r.Float64()
	f := map[string]any{
		"url": url, "rdomain": strings.Join(parts, ".") + ".", "path": path,
		"url_len": len(url), "path_depth": depth,
		"title": g.text(6), "body": g.text(40), "snippet": g.text(18),
		"status_code": r.Intn(4), "quality_score": quality,
		"static_rank": int(math.Max(1, math.Round(quality*10))) + r.Intn(3),
		"last_updated": 1735689600 + r.Int63n(60*86400*5),
		"original_count": 0, "original_authority": 0, "original_safe_miss_count": 0,
		"policy_domain_miss": 0, "original_safe_count": 0,
		"host_pagerank": r.Intn(100), "host_quality": r.Float64(), "tranco_rank": r.Intn(1000000),
		"language":         languages[r.Intn(len(languages))],
		"publication_date": time.Unix(1735689600+r.Int63n(300*86400), 0).UTC().Format("Jan 2, 2006"),
		"price":            math.Round(r.Float64()*100000) / 100, "status": r.Intn(5), "boost": r.Float64(),
	}
	if r.Intn(3) == 0 {
		f["image_url"] = "https://" + host + "/img/" + vocab[g.words.Uint64()] + ".jpg"
	}
	if n := r.Intn(4); n > 0 {
		s := make([]string, n)
		for i := range s {
			s[i] = "https://" + host + "/" + vocab[g.words.Uint64()]
		}
		f["sitelinks"] = s
	}
	// About a third of documents carry SERP-distilled signals, as distilled docs do in production.
	if r.Intn(3) == 0 {
		q := g.text(2)
		f["original_queries"] = map[string]int{vocab[g.words.Uint64()]: 1 + r.Intn(10), vocab[g.words.Uint64()]: 1 + r.Intn(10)}
		f["original_query_positions"] = map[string]int{q: 1 + r.Intn(10)}
		f["original_query_lexicon"] = []string{q}
		f["snippets_history"] = map[string]int{g.text(10): 1}
		c := 1 + r.Intn(20)
		f["original_count"], f["original_authority"], f["original_safe_count"] = c, r.Intn(40), c
		f["status_code"] = 0
	}
	return f
}

func cmdDocs(args []string) {
	fs := flag.NewFlagSet("docs", flag.ExitOnError)
	start := fs.Int64("start", 0, "first id")
	n := fs.Int64("n", 1000, "documents")
	fs.Parse(args)
	w := bufio.NewWriterSize(os.Stdout, 1<<20)
	defer w.Flush()
	enc := json.NewEncoder(w)
	for id := *start; id < *start+*n; id++ {
		g := newGen(id*7919 + 1)
		enc.Encode(map[string]any{"put": fmt.Sprintf("id:r6:webpage::%d", id), "fields": g.doc(id)})
	}
}

func cmdUpdates(args []string) {
	fs := flag.NewFlagSet("updates", flag.ExitOnError)
	rate := fs.Int("rate", 10000, "ops per second, 0 = unthrottled")
	dur := fs.Duration("dur", 60*time.Second, "duration")
	ndocs := fs.Int64("ndocs", 50000000, "id range")
	seed := fs.Int64("seed", time.Now().UnixNano(), "seed")
	fs.Parse(args)
	r := rand.New(rand.NewSource(*seed))
	w := bufio.NewWriterSize(os.Stdout, 64<<10)
	defer w.Flush()
	fields := []string{"price", "status", "boost"}
	emit := func(i int64) {
		id := r.Int63n(*ndocs)
		var v string
		switch fields[i%3] {
		case "price":
			v = strconv.FormatFloat(math.Round(r.Float64()*100000)/100, 'f', 2, 64)
		case "status":
			v = strconv.Itoa(r.Intn(5))
		default:
			v = strconv.FormatFloat(r.Float64(), 'f', 4, 64)
		}
		fmt.Fprintf(w, "{\"update\":\"id:r6:webpage::%d\",\"fields\":{\"%s\":{\"assign\":%s}}}\n", id, fields[i%3], v)
	}
	t0 := time.Now()
	var sent int64
	for time.Since(t0) < *dur {
		if *rate == 0 {
			for k := 0; k < 1000; k++ {
				emit(sent)
				sent++
			}
			continue
		}
		due := int64(time.Since(t0).Seconds() * float64(*rate))
		for ; sent < due; sent++ {
			emit(sent)
		}
		w.Flush()
		time.Sleep(2 * time.Millisecond)
	}
	w.Flush()
	el := time.Since(t0).Seconds()
	fmt.Fprintf(os.Stderr, "{\"emitted\":%d,\"seconds\":%.3f,\"emit_rate\":%.1f}\n", sent, el, float64(sent)/el)
}

func queryBody(r *rand.Rand, z *rand.Zipf) []byte {
	// Mid-frequency terms: skip the top 100 ranks (stopword-like), like real two-term queries.
	a, b := vocab[100+z.Uint64()], vocab[100+z.Uint64()]
	yql := fmt.Sprintf(`select * from webpage where ({targetHits:100}weakAnd(default contains "%s", default contains "%s", body contains "%s", body contains "%s")) and original_safe_miss_count = 0 and status < 4 and price < 900.0`, a, b, a, b)
	body, _ := json.Marshal(map[string]any{"yql": yql, "hits": 10, "ranking.profile": "r6", "timeout": "2s", "presentation.summary": "default"})
	return body
}

type qres struct {
	start, lat int64
	status     int
	hits       int
}

func doQuery(c *http.Client, url string, body []byte) (int, int) {
	resp, err := c.Post(url, "application/json", bytes.NewReader(body))
	if err != nil {
		return 0, -1
	}
	defer resp.Body.Close()
	data, _ := io.ReadAll(resp.Body)
	var out struct {
		Root struct {
			Fields struct {
				TotalCount int `json:"totalCount"`
			} `json:"fields"`
			Errors []any `json:"errors"`
		} `json:"root"`
	}
	json.Unmarshal(data, &out)
	if len(out.Root.Errors) > 0 && resp.StatusCode == 200 {
		return 299, out.Root.Fields.TotalCount
	}
	return resp.StatusCode, out.Root.Fields.TotalCount
}

func client() *http.Client {
	return &http.Client{Timeout: 5 * time.Second, Transport: &http.Transport{MaxIdleConns: 2048, MaxIdleConnsPerHost: 2048, DisableCompression: true}}
}

func cmdQuery(args []string) {
	fs := flag.NewFlagSet("query", flag.ExitOnError)
	qps := fs.Float64("qps", 500, "open-loop arrival rate")
	dur := fs.Duration("dur", 60*time.Second, "duration")
	url := fs.String("url", "http://localhost:8080/search/", "endpoint")
	out := fs.String("out", "-", "per-query CSV")
	seed := fs.Int64("seed", 1, "seed")
	fs.Parse(args)
	r := rand.New(rand.NewSource(*seed))
	z := rand.NewZipf(r, 1.07, 2, vocabSize-101)
	c := client()
	res := make(chan qres, 1<<16)
	var wg sync.WaitGroup
	t0 := time.Now()
	interval := time.Duration(float64(time.Second) / *qps)
	var inflight int64
	go func() {
		next := t0
		for time.Since(t0) < *dur {
			body := queryBody(r, z)
			if d := time.Until(next); d > 0 {
				time.Sleep(d)
			}
			// Latency counts from the scheduled send time, so client-side lag cannot hide queueing.
			sched := next
			next = next.Add(interval)
			wg.Add(1)
			atomic.AddInt64(&inflight, 1)
			go func() {
				defer wg.Done()
				defer atomic.AddInt64(&inflight, -1)
				st, hits := doQuery(c, *url, body)
				res <- qres{sched.Sub(t0).Microseconds(), time.Since(sched).Microseconds(), st, hits}
			}()
		}
		wg.Wait()
		close(res)
	}()
	var w io.Writer = os.Stdout
	if *out != "-" {
		f, err := os.Create(*out)
		if err != nil {
			panic(err)
		}
		defer f.Close()
		w = f
	}
	bw := bufio.NewWriter(w)
	defer bw.Flush()
	fmt.Fprintln(bw, "start_us,latency_us,status,hits")
	for q := range res {
		fmt.Fprintf(bw, "%d,%d,%d,%d\n", q.start, q.lat, q.status, q.hits)
	}
}

func cmdQmax(args []string) {
	fs := flag.NewFlagSet("qmax", flag.ExitOnError)
	conc := fs.Int("conc", 32, "clients")
	dur := fs.Duration("dur", 30*time.Second, "duration")
	url := fs.String("url", "http://localhost:8080/search/", "endpoint")
	fs.Parse(args)
	c := client()
	var mu sync.Mutex
	var lats []float64
	errs := 0
	var wg sync.WaitGroup
	t0 := time.Now()
	for i := 0; i < *conc; i++ {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			r := rand.New(rand.NewSource(int64(1000 + i)))
			z := rand.NewZipf(r, 1.07, 2, vocabSize-101)
			for time.Since(t0) < *dur {
				body := queryBody(r, z)
				s := time.Now()
				st, _ := doQuery(c, *url, body)
				l := float64(time.Since(s).Microseconds()) / 1000
				mu.Lock()
				if st != 200 {
					errs++
				}
				lats = append(lats, l)
				mu.Unlock()
			}
		}(i)
	}
	wg.Wait()
	sort.Float64s(lats)
	el := time.Since(t0).Seconds()
	p := func(q float64) float64 { return lats[int(q*float64(len(lats)-1))] }
	fmt.Printf("{\"conc\":%d,\"qps\":%.1f,\"p50_ms\":%.2f,\"p99_ms\":%.2f,\"errors\":%d,\"n\":%d}\n", *conc, float64(len(lats))/el, p(0.5), p(0.99), errs, len(lats))
}

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, "usage: r6load docs|updates|query|qmax ...")
		os.Exit(2)
	}
	switch os.Args[1] {
	case "vocab":
		for _, a := range os.Args[2:] {
			i, _ := strconv.Atoi(a)
			fmt.Println(vocab[i])
		}
	case "docs":
		cmdDocs(os.Args[2:])
	case "updates":
		cmdUpdates(os.Args[2:])
	case "query":
		cmdQuery(os.Args[2:])
	case "qmax":
		cmdQmax(os.Args[2:])
	default:
		fmt.Fprintln(os.Stderr, "unknown command", os.Args[1])
		os.Exit(2)
	}
}
