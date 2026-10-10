// SPDX-License-Identifier: MPL-2.0
// Narrow, imported-workbook edit; provision artifact-tool externally.
import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
const root=path.resolve(process.argv[2]||'.');
const {FileBlob,SpreadsheetFile}=await import(pathToFileURL(process.env.ARTIFACT_TOOL_MODULE).href);
const db=JSON.parse(await fs.readFile(path.join(root,'compatibility/gba-compatibility.json'),'utf8'));
const wb=await SpreadsheetFile.importXlsx(await FileBlob.load(path.join(root,'compatibility/GBA_Compatibility.xlsx')));
const out=path.join(root,'build-phase10/workbook');await fs.mkdir(out,{recursive:true});
async function preview(name,range,file){const b=await wb.render({sheetName:name,range,scale:1,format:'png'});await fs.writeFile(path.join(out,file),new Uint8Array(await b.arrayBuffer()));}
try {await fs.access(path.join(out,'before.png'));} catch {await preview('Summary','A1:I24','before.png');}
const keys=['bgm_verification_status','bgm_test_speeds','bgm_test_scope','bgm_tested_commit','bgm_evidence','se_verification_status'];
const selections={'All Games':db.records,'Limited Test Pass':db.records.filter(r=>r.public_status==='LIMITED_TEST_PASS'),
 'Experimental Unverified':db.records.filter(r=>r.public_status==='EXPERIMENTAL_UNVERIFIED'),
 'Native Fallback & Unsupported':db.records.filter(r=>['NATIVE_FALLBACK','UNSUPPORTED'].includes(r.public_status)),
 'Unknown & Not Analyzed':db.records.filter(r=>['UNKNOWN_DRIVER','NOT_ANALYZED'].includes(r.public_status))};
const enc=v=>typeof v==='object'?JSON.stringify(v):v;
for(const [name,rows] of Object.entries(selections)){
 const s=wb.worksheets.getItem(name),end=rows.length+5;
 const headers=s.getRange('A5:AD5').values[0];
 if(headers[24] && headers[24]!==keys[0])throw new Error('Unexpected original header');
 s.getRange('Y5:AD'+end).values=[keys,...rows.map(r=>keys.map(k=>enc(r[k])))];
 s.getRange('Y5:AD5').copyFrom(s.getRange('S5:X5'),'all');
 s.getRange('Y5:AD5').values=[keys];
 s.getRange('Y6:AD'+end).format={font:{name:'Arial',size:10,color:'#222222'},verticalAlignment:'center',wrapText:true};
 s.getRange('Y5:AD'+end).format.columnWidth=30;s.getRange('AA5:AA'+end).format.columnWidth=100;
 s.getRange('AB5:AB'+end).format.columnWidth=50;s.getRange('AC5:AC'+end).format.columnWidth=72;
 const t=s.tables.items[0];const tn=t.name,style=t.style;t.delete();
 const nt=s.tables.add('A5:AD'+end,true,tn);nt.style=style;nt.showFilterButton=true;
 rows.forEach((r,i)=>{if(r.bgm_verification_status!=='NOT_TESTED')s.getRange('Y'+(i+6)+':AD'+(i+6)).format.autofitRows();});
}
const s=wb.worksheets.getItem('Summary');s.getRange('A3').values=[['v0.4-preview RC / General statuses retained; BGM-only evidence appended']];
s.getRange('A24').values=[['Scoped 3x BGM evidence is separate; GB/GBC Fixed Audio remains research only.']];
s.getRange('A26:C29').values=[['BGM-only status','Identities','Scope'],['BGM_CONFIRMED',20,'Phase9 limited scenes; human listening pending'],['Other tested',6,'3 not active; 3 inconclusive'],['New catalog identities',0,'All26 exact identities already catalogued']];
s.getRange('A26:C29').format={font:{name:'Arial',size:10},rowHeight:32,wrapText:true};s.getRange('A26:C26').format={fill:'#334155',font:{name:'Arial',size:10,color:'#FFFFFF',bold:true}};
const m=wb.worksheets.getItem('Metadata Sources');m.getRange('A17:D18').values=[['BGM observations','Evidence','Scope','License'],['Phase9','docs/GBA_BGM_COVERAGE_PHASE9.json','Exact identity; normal sample-scale windows. No general/SE promotion.','CC BY-SA 4.0']];m.getRange('A17:D18').format.wrapText=true;
const defs=wb.worksheets.getItem('Status Definitions');defs.getRange('A21:B26').values=[
 ['BGM_CONFIRMED','Limited saved-scene1x/2x/3x unretimed BGM agreement. No full-game or SE guarantee; listening pending.'],
 ['BGM_INCORRECT','Measured BGM comparison failure. ACTIVE alone is insufficient.'],
 ['BGM_NOT_ACTIVE','Tested route did not sustain usable Fixed BGM.'],
 ['BGM_INCONCLUSIVE','BGM could not be isolated or compared reliably in the tested scene.'],
 ['SE_VERIFICATION','BGM tests do not promote SE acceptance. Finite SE can end early at2x/3x.'],
 ['FIXED_AUDIO_3X_LEGACY','General-validation status retained fromv0.3. Scoped3x evidence is in bgm_* fields.']];
defs.getRange('A21:B26').format={font:{name:'Arial',size:10},wrapText:true,rowHeight:70};
wb.recalculate();
console.log((await wb.inspect({kind:'match',searchTerm:'#REF!|#DIV/0!|#VALUE!|#NAME\\?|#NUM!|#SPILL!|#CALC!',options:{useRegex:true,maxResults:5},maxChars:600})).ndjson);
await preview('Summary','A1:I29','summary.png');
await preview('Experimental Unverified','Y5:AA8','bgm.png');
await preview('Status Definitions','A21:B26','bgm-definitions.png');
await (await SpreadsheetFile.exportXlsx(wb)).save(path.join(root,'compatibility/GBA_Compatibility.xlsx'));
