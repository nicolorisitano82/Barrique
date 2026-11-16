import sys,re,os
for t in sys.argv[1:]:
    d=os.path.dirname(t); n=os.path.basename(t)[:-4]
    rows=open(t).read().splitlines(); out=[rows[0]]
    for r in rows[1:]:
        f=r.split("\t"); p=os.path.join(d,"logs",f"{n}-{f[0]}.log")
        if f[1]=="ok" and f[3]=="-" and os.path.exists(p):
            m=re.findall(rb"tests executed \(\d+ marked as todo, \d+ as flaky, (\d+) failures?\)",open(p,"rb").read())
            if m and int(m[-1])>0: f[1]="fail"; f[3]=m[-1].decode(); print("fixed",t,f[0])
        out.append("\t".join(f))
    open(t,"w").write("\n".join(out)+"\n")
