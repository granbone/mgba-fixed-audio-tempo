"""Round-trip and publication checks for all generated compatibility formats.
SPDX-License-Identifier: MPL-2.0
Uses Python standard library only; does not access ROMs or execute games.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET
import zipfile
from build_compatibility_db import ROOT, FIELDS, STATUSES, load, summary
from generate_compatibility_outputs import cell

NS={'s':'http://schemas.openxmlformats.org/spreadsheetml/2006/main',
    'r':'http://schemas.openxmlformats.org/officeDocument/2006/relationships'}


def read_xlsx(path):
    with zipfile.ZipFile(path) as z:
        shared=[]
        if 'xl/sharedStrings.xml' in z.namelist():
            shared=[''.join(s.itertext()) for s in ET.fromstring(z.read('xl/sharedStrings.xml')).findall('s:si',NS)]
        relationships={r.attrib['Id']:r.attrib['Target'] for r in ET.fromstring(z.read('xl/_rels/workbook.xml.rels'))}
        sheets={}
        for s in ET.fromstring(z.read('xl/workbook.xml')).findall('s:sheets/s:sheet',NS):
            target=relationships[s.attrib['{'+NS['r']+'}id']]
            target=target.lstrip('/') if target.startswith('/') else 'xl/'+target
            doc=ET.fromstring(z.read(target));cells={}
            for c in doc.findall('.//s:sheetData/s:row/s:c',NS):
                value=c.find('s:v',NS);t=c.attrib.get('t')
                if t=='s':v=shared[int(value.text)] if value is not None else ''
                elif t=='inlineStr':v=''.join(c.find('s:is',NS).itertext())
                elif value is None:v=''
                elif t=='b':v=int(value.text)
                elif t=='e':raise AssertionError('XLSX formula error: '+value.text)
                elif t=='str':v=value.text or ''
                else:
                    try:v=float(value.text);v=int(v) if v.is_integer() else v
                    except (ValueError,TypeError):v=value.text or ''
                cells[c.attrib['r']]=v
            sheets[s.attrib['name']]=dict(cells=cells,xml=doc)
        return sheets


def column(n):
    s='';n+=1
    while n:n,x=divmod(n-1,26);s=chr(65+x)+s
    return s


def excel(value):
    if isinstance(value,bool):return int(value)
    if isinstance(value,(list,dict)):return json.dumps(value,ensure_ascii=False,separators=(',',':'))
    return '' if value is None else value


def main():
    from build_compatibility_db import FAMILIES
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=ROOT);p.add_argument('--report',type=Path)
    a=p.parse_args();root=a.root;db=load(root/'compatibility/gba-compatibility.json');rows=db['records']
    assert db['schema_version']==3 and db['summary']==summary(rows)
    assert len({r['release_id'] for r in rows})==len(rows)
    assert all(set(r)==set(FIELDS) for r in rows)
    dat=load(root/'compatibility/sources/no-intro-gba-releases.json')
    dat_index={(r['sha1'],r['rom_size']):r for r in dat['releases']}
    assert {(r['sha1'],r['rom_size']) for r in rows}==set(dat_index),'DAT-only catalogue mismatch'
    eligibility=load(root/'compatibility/sources/runtime-eligibility.json')
    assert eligibility['production_profile_sha256']==hashlib.sha256((root/'src/gba/mp2k-profile.c').read_bytes().replace(b'\r\n',b'\n')).hexdigest(),'Production eligibility evidence stale'
    eligible={r['sha256']:r for r in eligibility['records']}
    accepted={r['rom_sha256']:r for r in load(root/'compatibility/sources/integration-acceptance.json')['entries']}
    for r in rows:
        m=dat_index[(r['sha1'],r['rom_size'])]
        for key in ('title','region','revision','languages','crc32','game_code'):assert r[key]==m[key],(key,r['release_id'])
        assert r['driver_family'] in FAMILIES and r['public_status'] in STATUSES
        assert r['fixed_audio_3x']=='NOT_YET_VALIDATED'
        assert r['bgm_verification_status'] in ('NOT_TESTED','BGM_CONFIRMED','BGM_INCORRECT','BGM_NOT_ACTIVE','BGM_INCONCLUSIVE')
        assert r['se_verification_status']=='NOT_ASSESSED_BY_BGM_TEST'
        if r['bgm_verification_status']!='NOT_TESTED':
            assert r['bgm_test_speeds']==[1,2,3] and r['bgm_test_scope'] and len(r['bgm_tested_commit'])==40
            assert r['bgm_evidence'].startswith('compatibility/sources/v04-bgm-observations.json#')
        if r['public_status']=='NOT_ANALYZED':
            assert r['driver_family']=='NOT_ANALYZED' and not r['evidence_sources'] and r['sha256'] is None
        if r['public_status']=='EXPERIMENTAL_UNVERIFIED':
            assert r['runtime_eligibility']=='EXPERIMENTAL_TRIAL' and eligible[r['sha256']]['static_eligible'] is True
            assert r['fixed_audio_2x']=='NOT_TESTED','ACTIVE-only promotion'
        if r['public_status']=='LIMITED_TEST_PASS':
            assert r['sha256'] in accepted and 'INTEGRATION_PCM_EVENT_ACCEPTANCE' in r['evidence_sources']
            assert r['fixed_audio_2x']=='PASS' and r['runtime_eligibility']=='CONSERVATIVE_AND_EXPERIMENTAL'
        if r['runtime_eligibility']=='EXPERIMENTAL_TRIAL':assert eligible[r['sha256']]['static_eligible'] is True
        if r['public_status']=='UNKNOWN_DRIVER':assert r['driver_family']=='UNKNOWN_DRIVER','Detected driver mislabelled unknown'
        if r['public_status']=='NATIVE_FALLBACK':assert r['verification_status']=='NATIVE_FALLBACK_OBSERVED'
    assert {r['sha256'] for r in rows if r['public_status']=='LIMITED_TEST_PASS'}==set(accepted)
    with (root/'compatibility/gba-compatibility.csv').open(encoding='utf-8-sig',newline='') as f:
        reader=csv.DictReader(f);assert reader.fieldnames==FIELDS;csvrows=list(reader)
    assert csvrows==[{k:cell(r[k]) for k in FIELDS} for r in rows],'CSV round trip'
    sheets=read_xlsx(root/'compatibility/GBA_Compatibility.xlsx')
    names=['Summary','All Games','Limited Test Pass','Experimental Unverified','Native Fallback & Unsupported','Unknown & Not Analyzed','Status Definitions','Metadata Sources']
    assert list(sheets)==names
    selections={'All Games':rows,'Limited Test Pass':[r for r in rows if r['public_status']=='LIMITED_TEST_PASS'],
      'Experimental Unverified':[r for r in rows if r['public_status']=='EXPERIMENTAL_UNVERIFIED'],
      'Native Fallback & Unsupported':[r for r in rows if r['public_status'] in ('NATIVE_FALLBACK','UNSUPPORTED')],
      'Unknown & Not Analyzed':[r for r in rows if r['public_status'] in ('UNKNOWN_DRIVER','NOT_ANALYZED')]}
    for name,selected in selections.items():
        c=sheets[name]['cells'];doc=sheets[name]['xml']
        columns=[c.get(column(j)+'5','') for j in range(len(FIELDS))]
        assert len(set(columns))==len(FIELDS) and set(columns)==set(FIELDS)
        for i,r in enumerate(selected,6):
            for j,k in enumerate(columns):assert c.get(column(j)+str(i),'')==excel(r[k]),(name,i,k)
        assert not any(int(re.sub('[A-Z]','',addr))>len(selected)+5 and value!='' for addr,value in c.items()),name
        assert doc.find('.//s:pane',NS) is not None
        if selected:assert doc.find('s:tableParts',NS) is not None
    s=db['summary'];c=sheets['Summary']['cells']
    for i,family in enumerate(FAMILIES,6):
        x=s['driver_families'][family]
        assert c['A'+str(i)]==family
        values=[x[k] for k in STATUSES]+[x['experimental_trial_eligible'],x['total']]
        assert [c.get(column(j+1)+str(i)) for j in range(8)]==values,'XLSX driver count drift'
    totals=[*s['public_status'].values(),s['experimental_trial_eligible'],s['total_releases']]
    assert [c.get(column(j+1)+'13') for j in range(8)]==totals
    from generate_compatibility_outputs import table
    for name in ('COMPATIBILITY.md','README.md','README_JP.md'):
        text=(root/name).read_text(encoding='utf8')
        marker=re.search(r'<!-- compatibility-counts (.*?) -->',text)
        assert marker and json.loads(marker[1])==s and table(s) in text,name+' statistics drift'
    # Public-facing views must contain no ownership fields, old user statuses or private filenames.
    public=[root/'README.md',root/'README_JP.md',root/'COMPATIBILITY.md',root/'compatibility/METADATA_SOURCES.md',
        root/'compatibility/gba-compatibility.json',root/'compatibility/gba-compatibility.csv',root/'compatibility/GBA_Compatibility.xlsx',
        root/'.github/ISSUE_TEMPLATE/compatibility-report.yml',root/'docs/COMMUNITY_COMPATIBILITY_REPORTS.md']
    private=re.compile(r'(?<![A-Za-z])[A-Z]:[\\/](?!/)|github_pat_[A-Za-z0-9_]{20,}|gh[pousr]_[A-Za-z0-9]{20,}|sk-[A-Za-z0-9_-]{30,}|-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----')
    obsolete=re.compile(r'\b(?:HUMAN_VALIDATED|HUMAN_TEST_READY|AUTO_PASS|CANDIDATE)\b|local_rom_available|local_rom_identities|metadata_only_releases|"filename"')
    for path in public:
        if path.suffix=='.xlsx':
            with zipfile.ZipFile(path) as z:contents=[z.read(n).decode('utf8') for n in z.namelist() if n.endswith('.xml')]
        else:contents=[path.read_text(encoding='utf-8-sig')]
        for text in contents:assert not private.search(text) and not obsolete.search(text),(path.name,'privacy/obsolete field')
    result=dict(passed=True,total_records=len(rows),summary=s,dat_only=True,identity_deduplicated=True,
        all_fields_round_trip=True,subset_membership_equal=True,summary_counts_equal=True,readme_counts_equal=True,
        eligibility_matches_production_profile=True,no_active_only_promotion=True,no_regional_result_transfer=True,
        public_ownership_statistics=False,public_private_filenames=False,public_old_statuses=False,
        database_sha256=hashlib.sha256((root/'compatibility/gba-compatibility.json').read_bytes()).hexdigest())
    if a.report:a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8')
    print(json.dumps(result,indent=2))
if __name__=='__main__':main()
