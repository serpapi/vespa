import re, sys, statistics as st
rows=[]
for path in sys.argv[1:]:
    for line in open(path):
        if not line.startswith('rep='): continue
        d=dict(kv.split('=',1) for kv in line.split() if '=' in kv)
        rows.append(d)
def q(xs, p):
    xs=sorted(xs); k=(len(xs)-1)*p; f=int(k); c=min(f+1,len(xs)-1); return xs[f]+(xs[c]-xs[f])*(k-f)
print(f"{'phase':6} {'var':3} {'n':>3} {'median_s':>9} {'IQR_s':>17} {'ns/lid':>7} {'minflt':>7} {'peakΔMB':>8} {'gen':>4} bytes")
groups={}
for r in rows: groups.setdefault((r['phase'],r['variant']),[]).append(r)
for (ph,v),rs in sorted(groups.items(), key=lambda kv:(['clone','ctor','grow','memcpy'].index(kv[0][0]),['A','B','C0','C'].index(kv[0][1]))):
    w=[float(r['wall_s']) for r in rs]; n=int(rs[0]['n'])
    med=st.median(w); q1=q(w,.25); q3=q(w,.75)
    flt=sorted(set(r['minflt'] for r in rs)); gen=sorted(set(r['gen'] for r in rs)); b=sorted(set(r['bytes'] for r in rs))
    peak=[(int(r['hwm_after_kb'])-int(r['rss_before_kb']))/1024 for r in rs]
    print(f"{ph:6} {v:3} {len(w):3} {med:9.4f} [{q1:7.4f},{q3:7.4f}] {med*1e9/n:7.3f} {','.join(flt):>7} {st.median(peak):8.0f} {','.join(gen):>4} {','.join(b)}")
