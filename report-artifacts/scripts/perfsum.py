import csv,glob,re,os
res={}
for f in sorted(glob.glob('/work/results/perfstat-d*.csv')):
    m=re.match(r'.*perfstat-d(\d+)-k(\d+)-s([\d.]+)-u(\d)-(legacy|current)\.csv',f)
    if not m: continue
    docs,k,s,u,impl=m.groups()
    ev={}
    for line in open(f):
        p=line.strip().split(',')
        if len(p)<3 or line.startswith('#'): continue
        try: ev[p[2]]=float(p[0])
        except: ev[p[2]]=float('nan')
    txt=f[:-4]+'.txt'
    hits=reps=None; ns=None
    for line in open(txt):
        if line.startswith('CSV,zc'):
            q=line.strip().split(','); hits=int(q[6]); reps=int(q[7]); ns=(float(q[8]),float(q[9]))
    n=hits*reps
    res[(int(docs),int(k),u,impl)]=(ev,n,ns)
print("docs k unpack impl | ns/hit(min,med) | cyc/hit instr/hit IPC | br-miss/hit | L1D-miss/hit L2-miss/hit")
for key in sorted(res):
    ev,n,ns=res[key]
    c=ev['cycles']/n; i=ev['instructions']/n
    print(f"{key[0]//1000000:4}M {key[1]:2} {key[2]} {key[3]:7} | {ns[0]:6.2f} {ns[1]:6.2f} | {c:6.1f} {i:6.1f} {i/c*1:4.2f} | {ev['branch-misses']/n:5.3f} | {ev['l1d_miss']/n:5.2f} {ev['l2_miss']/n:5.2f}")
