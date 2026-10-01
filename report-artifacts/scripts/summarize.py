import csv,sys
rows=[r for r in csv.reader(open(sys.argv[1])) if r and r[0]=="CSV" and r[1]!="children"]
d={}
for r in rows:
    d[(r[1],r[2],r[3],r[4],r[5])]=(float(r[8]),float(r[9]),r[6])
print("children k stale unpack | legacy min/med | new min/med | new/legacy(min) | hits")
for (c,k,s,u,i),(mn,md,h) in sorted(d.items(), key=lambda x:(x[0][0],int(x[0][1]),float(x[0][2]),x[0][3])):
    if i!="legacy": continue
    o=d.get((c,k,s,u,"current"))
    if not o: continue
    print(f"{c:5} {k:>2} {s:>5} {u} | {mn:6.2f} {md:6.2f} | {o[0]:6.2f} {o[1]:6.2f} | {o[0]/mn:5.3f} | {h}")
