#!/usr/bin/env python3
# usage: targets.py <tree> -> line 1: make targets, line 2: ctest names (no BENCHMARK tests)
import os,re,sys
tree=sys.argv[1]
dirs=['searchlib/src/tests/features','searchlib/src/tests/queryeval','searchlib/src/tests/diskindex']
exes=[]; tests=[]
def calls(text,fn):
    for m in re.finditer(fn+r'\s*\(',text):
        i=m.end(); depth=1; j=i
        while depth:
            depth+= {'(':1,')':-1}.get(text[j],0); j+=1
        yield text[i:j-1]
for d in dirs:
    for root,_,files in os.walk(os.path.join(tree,d)):
        if 'CMakeLists.txt' in files:
            text=open(os.path.join(root,'CMakeLists.txt')).read()
            text=re.sub(r'#[^\n]*','',text)
            for c in calls(text,'vespa_add_executable'):
                exes.append(c.split()[0])
            for c in calls(text,'vespa_add_test'):
                toks=c.split()
                if 'BENCHMARK' in toks: continue
                tests.append(toks[toks.index('NAME')+1])
extra_exes=['searchlib_vespa-index-inspect_app','searchcore_query_test_app','searchcore_querynodes_test_app',
            'searchcore_matching_test_app','searchcore_indexmanager_test_app','searchcore_fusionrunner_test_app',
            'searchcore_indexcollection_test_app']
extra_tests=[e for e in extra_exes if e.startswith('searchcore_')]
print(' '.join(sorted(set(exes+extra_exes))))
print(' '.join(sorted(set(tests+extra_tests))))
