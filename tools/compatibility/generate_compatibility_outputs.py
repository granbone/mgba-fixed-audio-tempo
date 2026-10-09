"""Generate every public view from canonical JSON. SPDX-License-Identifier: MPL-2.0"""
import argparse,csv,hashlib,json,re,subprocess
from pathlib import Path
from build_compatibility_db import ROOT,FIELDS,STATUSES,FAMILIES,load
def cell(v):
 if v is None:return ''
 if isinstance(v,(list,dict)):return json.dumps(v,ensure_ascii=False,separators=(',',':'))
 return str(v)
def table(s):
 lines=['| Driver Family | Limited Test Pass | Experimental Unverified | Native Fallback | Unsupported | Unknown Driver | Not Analyzed | Trial eligible | Total |',
 '| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |']
 for f in FAMILIES:
  x=s['driver_families'][f]
  lines.append('| '+f+' | '+' | '.join(str(x[k]) for k in [*STATUSES,'experimental_trial_eligible','total'])+' |')
 lines.append('| Total | '+' | '.join(str(v) for v in [*s['public_status'].values(),s['experimental_trial_eligible'],s['total_releases']])+' |')
 return '\n'.join(lines)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--root',type=Path,default=ROOT);p.add_argument('--xlsx',action='store_true');a=p.parse_args();root=a.root
 db=load(root/'compatibility/gba-compatibility.json');rows=db['records'];s=db['summary']
 with (root/'compatibility/gba-compatibility.csv').open('w',encoding='utf-8-sig',newline='') as f:
  w=csv.DictWriter(f,fieldnames=FIELDS);w.writeheader();w.writerows({k:cell(r[k]) for k in FIELDS} for r in rows)
 trial=[r for r in rows if r['runtime_eligibility'] in ('EXPERIMENTAL_TRIAL','CONSERVATIVE_AND_EXPERIMENTAL')]
 breakdown={k:sum(r['public_status']==k for r in trial) for k in STATUSES}
 fallback_codes=', '.join(r['game_code'] for r in trial if r['public_status']=='NATIVE_FALLBACK')
 detail=(f"Trial eligible = {breakdown['LIMITED_TEST_PASS']} limited passes + {breakdown['EXPERIMENTAL_UNVERIFIED']} unverified trials + {breakdown['NATIVE_FALLBACK']} observed fallback ({fallback_codes}). Static trial eligibility can coexist with native fallback in a tested scene; neither detection nor ACTIVE is an audio test pass. "
         '静的に試行可能でも、試験した場面では通常音声になる場合があります。ACTIVEだけで音声試験PASSとは認定しません。')
 stats=table(s)+'\n\nCounts refer to unique public DAT release identities. Regional releases and revisions do not inherit test results. Trial eligible includes limited-pass identities; it is not an additional status. NOT_ANALYZED is shown separately.\n\n'+detail+'\n\n<!-- compatibility-counts '+json.dumps(s,sort_keys=True,separators=(',',':'))+' -->'
 md=['# GBA Fixed Audio Compatibility — v0.3-preview candidate','',
 '[All games: Excel](compatibility/GBA_Compatibility.xlsx) · [JSON](compatibility/gba-compatibility.json) · [CSV](compatibility/gba-compatibility.csv)','',
 'Search the DAT title, region and revision. Driver detection, Experimental trial eligibility and actual limited audio verification are separate fields. A matching title or game code never transfers results across ROM hashes.','',stats,'',
 db['limited_test_scope']['en'],'',db['limited_test_scope']['ja'],'',
 'Experimental Unverified: Fixed Audioを試行する機能はありますが、正常動作は未確認です。 Fixed Audio can be tried; correct behavior is unverified. Live validation may select native audio.','',
 db['warnings']['en'],'',db['warnings']['ja'],'','## Mode behavior','',
 'The new default is Experimental — All Detected Drivers. It tries implemented MP2K ROM_PLAYER, validated EWRAM structures and the exact B6JJ backend. Conservative — Tested Drivers Only uses exact limited-test identities. Disabled uses native audio. Reload content after changing the mode.','',
 'Static profile rejection and observed native fallback are different results. Unknown drivers are not assumed unsupported. Unanalyzed DAT identities are not assumed eligible.','',
 '3x and higher remain unvalidated and use native audio. GB/GBC Fixed Audio remains research only. FFTA/AFXJ late-state reconstruction can retain native audio for about 18 seconds. Failed reconstruction safely stops the candidate route.','',
 '## Status definitions','']
 for k,v in db['status_definitions'].items():md += [f'**{k}** — {v}','']
 md += ['## Metadata and reports','',
 'Titles, regions and named revisions come exclusively from the pinned public No-Intro DAT. Header revisions are separate. Unmatched analysis stays in private ignored reports. [Sources and licenses](compatibility/METADATA_SOURCES.md).','',
 'Use the [compatibility report form](.github/ISSUE_TEMPLATE/compatibility-report.yml). Include exact hashes, mode, speed, core version and scenario. Do not attach ROMs, BIOS files, saves or savestates.','']
 (root/'COMPATIBILITY.md').write_text('\n'.join(md),encoding='utf8')
 for name in ('README.md','README_JP.md'):
  path=root/name;text=path.read_text(encoding='utf8')
  block='<!-- compatibility-summary:start -->\n'+stats+'\n<!-- compatibility-summary:end -->'
  if '<!-- compatibility-summary:start -->' in text:
   text=re.sub(r'<!-- compatibility-summary:start -->.*?<!-- compatibility-summary:end -->',lambda m:block,text,flags=re.S)
  else:text += '\n'+block+'\n'
  path.write_text(text,encoding='utf8')
 src=['# Metadata Sources / メタデータ出典','',
 'Public names are from No-Intro, distributed by libretro/libretro-database. Only text metadata was retrieved. No game binaries were downloaded.','']
 for x in db['sources']:
  src += [f"## {x['id']}",'',f"Source: [{x['id']}]({x['url']})",f"Acquired/imported: {x['acquired']}",f"License: {x['license']}",f"Scope: {x['scope']}"]
  if x.get('revision'):src.append('Pinned revision: '+x['revision'])
  if x.get('sha256'):src.append('DAT SHA256: '+x['sha256'])
  src.append('')
 src += ['## Attribution and redistribution audit — 2026-10-09','',
 'The pinned libretro database [LICENSE](https://github.com/libretro/libretro-database/blob/fbeefcb46c2e1b20a7e2945f34a694a41b2d6f90/LICENSE) is Creative Commons Attribution-ShareAlike 4.0 International. Original texts are retained in compatibility/LICENSE and LICENSES/CC-BY-SA-4.0.txt. Credit No-Intro and libretro database contributors, retain the source and license links and modification notice, and share adaptations under CC BY-SA 4.0. No endorsement is implied.','',
 'Modifications: selected serial-bearing released metadata, excluded labelled non-retail external entries, normalized fields, joined exact hash evidence, deduplicated identities, and added scenario-specific driver/runtime classifications. Unmatched records are excluded from the public view. No related region inherits a test result.','',
 'The [No-Intro Data Usage License](https://datomatic.no-intro.org/terms.html), checked 2026-10-09 (last updated 2026-09-11), permits lawful reuse within rights controlled by its operator. The libretro attribution/share-alike terms are preserved. Game names and trademarks belong to their owners. Metadata licenses do not authorize game, firmware, image, music or video redistribution. No such material is included.','',
 'The DAT scope is explicit in the source snapshot. Entries without a serial and labelled prototype/demo/beta/unlicensed/BIOS/test records were excluded; this can omit legitimate releases and is not a completeness claim. Regional binaries sharing one SHA1/size count once.','',
 'Join order: full SHA1 with size and CRC32 consistency; uniquely matching CRC32+size only when a full hash is unavailable; game code is a supplemental consistency check. A contradictory full hash never falls back to a weaker match. Title similarity is never a join key. DAT revision text is preserved; ROM header revision is separately recorded.','',
 'Static eligibility is measured by the production bridge scanner and core profile builder. It confirms a trial path, not live structure validity, bridge success or audio accuracy. The public database contains no ownership statistics or filenames. Original internal evidence and unmatched reports remain private.','',
 'The compatibility data and evidence snapshots are CC BY-SA 4.0. Source code remains MPL-2.0 or its existing file license; separate bridge/agbplay code remains LGPLv3. The v0.3 RC is prepared locally and is not published. Its package includes matching editable sources, dependency notices and relink support as described in THIRD_PARTY_NOTICES.md.','']
 (root/'compatibility/METADATA_SOURCES.md').write_text('\n'.join(src),encoding='utf8')
 properties={k:{'type':'string'} for k in FIELDS}
 for k in ('revision','header_revision','sha256','tested_core_version'):properties[k]={'type':['string','integer','null'] if k in ('revision','header_revision') else ['string','null']}
 for k in ('languages','evidence_sources'):properties[k]={'type':'array','items':{'type':'string'}}
 properties['rom_size']={'type':'integer','minimum':1}
 properties['game_code']={'type':['string','null'],'pattern':'^[A-Z0-9]{4}$'}
 properties['public_status']={'enum':list(STATUSES)}
 properties['driver_family']={'enum':FAMILIES}
 properties['runtime_eligibility']={'enum':['NOT_ESTABLISHED','EXPERIMENTAL_TRIAL','CONSERVATIVE_AND_EXPERIMENTAL','STATIC_REJECTED','NO_IMPLEMENTED_BACKEND']}
 properties['fixed_audio_3x']={'const':'NOT_YET_VALIDATED'}
 for k,n in [('crc32',8),('sha1',40),('sha256',64)]:properties[k]={'type':['string','null'] if k=='sha256' else 'string','pattern':f'^[0-9A-F]{{{n}}}$'}
 schema={'$schema':'https://json-schema.org/draft/2020-12/schema','title':'GBA Compatibility v2','type':'object',
  'required':['schema_version','summary','records'],'properties':{'schema_version':{'const':2},
  'records':{'type':'array','items':{'type':'object','required':FIELDS,'properties':properties,'additionalProperties':False}}}}
 (root/'compatibility/schema.json').write_text(json.dumps(schema,indent=2)+'\n',encoding='utf8')
 if a.xlsx:subprocess.run(['node',str(root/'tools/compatibility/build_compatibility_workbook.mjs'),str(root)],check=True)
 print('Generated CSV, Markdown, README statistics and schema:',len(rows))
if __name__=='__main__':main()
