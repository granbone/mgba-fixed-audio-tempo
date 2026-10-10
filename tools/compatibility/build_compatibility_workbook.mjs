// SPDX-License-Identifier: MPL-2.0
// Reproducible data/formulas from canonical JSON. Provision artifact-tool externally.
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const root=path.resolve(process.argv[2]||'.');
const engine=process.env.ARTIFACT_TOOL_MODULE;
const {Workbook,SpreadsheetFile}=engine?await import(pathToFileURL(engine).href):await import('@oai/artifact-tool');
const db=JSON.parse(await fs.readFile(path.join(root,'compatibility/gba-compatibility.json'),'utf8'));
const out=path.join(root,'build-public-presentation/outputs/presentation-v03');
await fs.mkdir(out,{recursive:true});
const wb=Workbook.create();
const names=['Summary','All Games','Limited Test Pass','Experimental Unverified','Native Fallback & Unsupported','Unknown & Not Analyzed','Status Definitions','Metadata Sources'];
const sheets=Object.fromEntries(names.map(n=>[n,wb.worksheets.add(n)]));
const first=['title','region','revision','driver_family','public_status','fixed_audio_2x','fixed_audio_3x','state_load','rewind','runtime_eligibility','verification_status','tested_core_version','notes'];
const fields=[...first,...Object.keys(db.records[0]).filter(k=>!first.includes(k))];
const enc=v=>v==null?'':typeof v==='object'?JSON.stringify(v):v;
function col(n){let s='';for(++n;n;n=Math.floor((n-1)/26))s=String.fromCharCode(65+(n-1)%26)+s;return s;}
function header(s,r){s.getRange(r).format={fill:'#334155',font:{name:'Arial',size:10,bold:true,color:'#FFFFFF'},rowHeight:44,wrapText:true,horizontalAlignment:'center',verticalAlignment:'center'};}
function title(s,t,note,end='F'){
 s.showGridLines=false;
 s.getRange('A2').values=[[t]];s.getRange('A2').format.font={name:'Arial',size:15,bold:true,color:'#222222'};
 s.getRange('A2:'+end+'2').format.rowHeight=27;s.getRange('A2:'+end+'2').format.borders={preset:'doubleBottom',style:'thin',color:'#CBD5E1'};
 s.getRange('A3').values=[[note]];s.getRange('A3').format.font={name:'Arial',size:10,italic:true,color:'#555555'};s.getRange('A3:'+end+'3').format.rowHeight=24;
}
function grid(name,rows){
 const s=sheets[name],last=col(fields.length-1),end=rows.length+5;
 title(s,name,'Exact DAT identity / DAT由来identity。検出・試行資格・限定試験を区別します。',last);
 s.getRange('A5:'+last+end).values=[fields,...rows.map(r=>fields.map(k=>enc(r[k])))];
 s.getRange('A5:'+last+end).format.font={name:'Arial',size:10,color:'#222222'};
 s.getRange('A6:'+last+end).format.rowHeight=38;s.getRange('A6:'+last+end).format.verticalAlignment='center';
 for(let j=0;j<fields.length;j++){
  const k=fields[j],r=s.getRange(col(j)+'5:'+col(j)+end);
  r.format.columnWidth=k==='title'?62:k==='region'?20:k==='revision'?15:k==='driver_family'?29:k==='public_status'?32:k==='notes'?95:k==='metadata_source'?100:k==='sha256'?70:k==='sha1'?46:k==='release_id'?49:k==='rom_size'?15:26;
  if(['title','driver_family','public_status','runtime_eligibility','verification_status','fallback_behavior','tested_core_version','notes','metadata_source','evidence_sources'].includes(k))r.format.wrapText=true;
  if(k==='evidence_sources')r.format.columnWidth=68;
  if(['release_id','crc32','sha1','sha256','game_code'].includes(k))r.setNumberFormat('@');
  if(k==='rom_size')r.setNumberFormat('#,##0');
 }
 s.getRange('A6:'+last+end).format.autofitRows();
 if(rows.length){const t=s.tables.add('A5:'+last+end,true,'Games'+names.indexOf(name));t.showFilterButton=true;}
 header(s,'A5:'+last+'5');s.freezePanes.freezeRows(5);s.freezePanes.freezeColumns(2);
 if(rows.length){
  const r=s.getRange(col(fields.indexOf('public_status'))+'6:'+col(fields.indexOf('public_status'))+end);
  for(const [text,fill] of [['LIMITED_TEST_PASS','#DCECE1'],['EXPERIMENTAL_UNVERIFIED','#FFF0CC'],['NATIVE_FALLBACK','#F7E4DD'],['UNSUPPORTED','#F0DADA'],['UNKNOWN_DRIVER','#EFE7F5'],['NOT_ANALYZED','#EDF0F4']])
   r.conditionalFormats.add('containsText',{text,format:{fill}});
 }
 return end;
}
const allEnd=grid('All Games',db.records);
grid('Limited Test Pass',db.records.filter(r=>r.public_status==='LIMITED_TEST_PASS'));
grid('Experimental Unverified',db.records.filter(r=>r.public_status==='EXPERIMENTAL_UNVERIFIED'));
grid('Native Fallback & Unsupported',db.records.filter(r=>['NATIVE_FALLBACK','UNSUPPORTED'].includes(r.public_status)));
grid('Unknown & Not Analyzed',db.records.filter(r=>['UNKNOWN_DRIVER','NOT_ANALYZED'].includes(r.public_status)));
const s=sheets.Summary;title(s,'GBA Fixed Audio Compatibility',db.version_candidate+' / Unique public DAT release identities','I');s.tabColor='#334155';
const statusCol=col(fields.indexOf('public_status')),familyCol=col(fields.indexOf('driver_family')),trialCol=col(fields.indexOf('runtime_eligibility'));
const range=k=>"'All Games'!$"+k+"$6:$"+k+"$"+allEnd;
const statuses=Object.keys(db.status_definitions),families=Object.keys(db.summary.driver_families);
s.getRange('A5:I5').values=[['Driver Family',...statuses,'Trial eligible','Total']];
s.getRange('A6:A12').values=families.map(f=>[f]);
s.getRange('A5:I13').format.font={name:'Arial',size:10,color:'#222222'};
s.getRange('A5:A13').format.columnWidth=35;s.getRange('B5:I13').format.columnWidth=20;
s.getRange('A6:I13').format.rowHeight=36;s.getRange('A6:A13').format.wrapText=true;
for(let i=0;i<families.length;i++){
 const n=i+6;
 s.getRange('B'+n+':G'+n).formulas=[statuses.map((v,j)=>'=COUNTIFS('+range(familyCol)+',$A'+n+','+range(statusCol)+','+col(j+1)+'$5)')];
 s.getRange('H'+n).formulas=[['=COUNTIFS('+range(familyCol)+',$A'+n+','+range(trialCol)+',"EXPERIMENTAL_TRIAL")+COUNTIFS('+range(familyCol)+',$A'+n+','+range(trialCol)+',"CONSERVATIVE_AND_EXPERIMENTAL")']];
 s.getRange('I'+n).formulas=[['=SUM(B'+n+':G'+n+')']];
}
s.getRange('A13').values=[['Total']];
s.getRange('B13:I13').formulas=[[...Array.from({length:8},(_,j)=>'=SUM('+col(j+1)+'6:'+col(j+1)+'12)')]];
s.getRange('B6:I13').setNumberFormat('#,##0');header(s,'A5:I5');
s.getRange('A13:I13').format.font.bold=true;s.getRange('A13:I13').format.borders={preset:'doubleBottom',style:'thin',color:'#CBD5E1'};
const trialRows=db.records.filter(r=>['EXPERIMENTAL_TRIAL','CONSERVATIVE_AND_EXPERIMENTAL'].includes(r.runtime_eligibility));
const trialCount=status=>trialRows.filter(r=>r.public_status===status).length;
const trialFallbackCodes=trialRows.filter(r=>r.public_status==='NATIVE_FALLBACK').map(r=>r.game_code).sort().join(', ');
const summaryNotes=['Trial eligible includes Limited Test Pass. It is not another status.',
 `${trialRows.length} trial eligible = ${trialCount('LIMITED_TEST_PASS')} limited pass + ${trialCount('EXPERIMENTAL_UNVERIFIED')} unverified + ${trialCount('NATIVE_FALLBACK')} observed fallback.`,
 `Trial-eligible fallback identities: ${trialFallbackCodes}. Static eligibility does not guarantee ACTIVE.`,
 'NOT_ANALYZED: no exact driver analysis. UNKNOWN_DRIVER: analyzed, unidentified.',
 'Limited pass covers specific scenes only. See Status Definitions.',
 '限定試験は全編クリアや全BGM・SEの正常動作を保証しません。',
 'Experimental can fall back at runtime. Back up saves and states.',
 '実験的対応は動作未確認です。試用前にバックアップを作成してください。',
 'Scoped3x BGM evidence is separate from general/SE validation. GB/GBC: research only.'];
s.getRange('A16:A'+(15+summaryNotes.length)).values=summaryNotes.map(x=>[x]);s.getRange('A16:I'+(15+summaryNotes.length)).format.font={name:'Arial',size:10,color:'#333333'};s.getRange('A16:I'+(15+summaryNotes.length)).format.rowHeight=24;
const d=sheets['Status Definitions'];title(d,'Status Definitions / 対応状況','Detection, trial eligibility and observed audio are separate.');
const defs=[...Object.entries(db.status_definitions),['LIMITED_TEST_SCOPE_EN',db.limited_test_scope.en],['LIMITED_TEST_SCOPE_JA',db.limited_test_scope.ja],
 ['EXPERIMENTAL_WARNING_EN',db.warnings.en],['EXPERIMENTAL_WARNING_JA',db.warnings.ja],
 ['CONSERVATIVE_AND_EXPERIMENTAL','Exact limited-test identity eligible in both enabled modes; runtime safety checks still apply.'],
 ['EXPERIMENTAL_TRIAL','Static production eligibility passed; Conservative excludes this identity. Live player/track validation and bridge initialization remain required.'],
 ['STATIC_REJECTED','Current production backend rejects static data; no forced activation.'],
 ['NOT_ESTABLISHED','No verified backend eligibility for this identity. Not a proof of unsupported behavior.']];
d.getRange('A5:B'+(defs.length+5)).values=[['Status / Field','Definition / 説明'],...defs];
d.getRange('A5:B'+(defs.length+5)).format.font={name:'Arial',size:10,color:'#222222'};
d.getRange('A5:A'+(defs.length+5)).format.columnWidth=36;d.getRange('B5:B'+(defs.length+5)).format.columnWidth=115;
d.getRange('A6:B'+(defs.length+5)).format.wrapText=true;d.getRange('A6:B'+(defs.length+5)).format.rowHeight=70;header(d,'A5:B5');d.freezePanes.freezeRows(5);
const m=sheets['Metadata Sources'];title(m,'Metadata Sources / 出典','Text metadata only. Attribution and license terms retained.','D');
const sourceRows=db.sources.map(x=>[x.id,x.url,x.acquired,x.license+'; '+x.scope]);
m.getRange('A5:D'+(sourceRows.length+5)).values=[['Source','URL','Acquired / imported','License and scope'],...sourceRows];
m.getRange('A5:D15').format.font={name:'Arial',size:10,color:'#222222'};
m.getRange('A5:A15').format.columnWidth=32;m.getRange('B5:B15').format.columnWidth=98;m.getRange('C5:C15').format.columnWidth=24;m.getRange('D5:D15').format.columnWidth=88;
m.getRange('A6:D8').format.wrapText=true;m.getRange('A6:D8').format.rowHeight=95;header(m,'A5:D5');
m.getRange('A10:A15').values=[['Pinned DAT revision'],['DAT SHA256'],['Attribution'],['Redistribution'],['No-Intro terms'],['Modification notice']];
m.getRange('B10:B15').values=[[db.sources[0].revision],[db.sources[0].sha256],['No-Intro and libretro database contributors; no endorsement implied.'],
 ['CC BY-SA 4.0; retain attribution, license and changes; share adaptations alike.'],
 ['https://datomatic.no-intro.org/terms.html (checked 2026-10-09)'],
 ['Selected released identities, normalized fields, exact hash joins and scenario classifications. Unmatched records excluded.']];
m.getRange('A10:B15').format.rowHeight=40;m.getRange('B10:B15').format.wrapText=true;
wb.recalculate();
const actual=s.getRange('B6:I12').values;
const expected=families.map(f=>{const x=db.summary.driver_families[f];return [...statuses.map(k=>x[k]),x.experimental_trial_eligible,x.total];});
if(JSON.stringify(actual)!==JSON.stringify(expected))throw new Error('Summary mismatch '+JSON.stringify(actual));
console.log((await wb.inspect({kind:'region',sheetId:'Summary',range:'A5:I13',maxChars:2200,tableMaxRows:9,tableMaxCols:9})).ndjson);
console.log((await wb.inspect({kind:'match',searchTerm:'#REF!|#DIV/0!|#VALUE!|#NAME\\?|#NUM!|#SPILL!|#CALC!',options:{useRegex:true,maxResults:10},maxChars:1000})).ndjson);
for(const n of names){
 const range=n==='Summary'?'A1:I'+(15+summaryNotes.length):n==='Status Definitions'?'A1:B11':n==='Metadata Sources'?'A1:D15':'A1:F10';
 const blob=await wb.render({sheetName:n,range,scale:1,format:'png'});
 await fs.writeFile(path.join(out,n.replaceAll(' ','_')+'.png'),new Uint8Array(await blob.arrayBuffer()));
}
const notePreview=await wb.render({sheetName:'Experimental Unverified',range:'L5:M8',scale:1,format:'png'});
await fs.writeFile(path.join(out,'Notes_detail.png'),new Uint8Array(await notePreview.arrayBuffer()));
const xlsx=await SpreadsheetFile.exportXlsx(wb);await xlsx.save(path.join(root,'compatibility/GBA_Compatibility.xlsx'));
await fs.rename(path.join(root,'compatibility/GBA_Compatibility.xlsx.inspect.ndjson'),path.join(out,'GBA_Compatibility.xlsx.inspect.ndjson')).catch(e=>{if(e.code!=='ENOENT')throw e;});
console.log(JSON.stringify({records:db.records.length,sheets:names,output:'compatibility/GBA_Compatibility.xlsx'}));
