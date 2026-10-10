"""Exercise escaped JSON/UNC/UTF16 and nested archive privacy detection.
SPDX-License-Identifier: MPL-2.0. Synthetic in-memory data only.
"""
import io,json,zipfile
import audit_public as audit
def rejected(name,data):
 try:audit.inspect(name,data)
 except AssertionError:return
 raise AssertionError('Expected rejection: '+name)
def main():
 slash=chr(92);drive=chr(74)+':';path=drive+slash+'private'+slash+'fixture'
 unc=slash*2+'AS'+str(1234)+slash+'private'
 user=chr(67)+':'+slash+'Users'+slash+'test'
 for value in (path,unc,user):
  rejected('plain.txt',value.encode())
  rejected('utf16.txt',value.encode('utf-16le'))
  rejected('escaped.json',json.dumps({'path':value}).encode())
  rejected('escaped-key.json',json.dumps({value:'x'}).encode())
 encoded=json.dumps({'path':path}).replace(drive,chr(74)+'\\u003a').replace('private','\\u0070rivate').encode()
 assert json.loads(encoded)['path']==path
 rejected('unicode.json',encoded)
 z=io.BytesIO()
 with zipfile.ZipFile(z,'w') as archive:archive.writestr('inner.json',json.dumps({'path':path}))
 rejected('outer.zip',z.getvalue())
 audit.inspect('relative.json',json.dumps({'path':'output/fixture.json','commit':'a'*40,'version':'v0.4-preview'}).encode())
 print('PUBLIC AUDIT TEST PASS: plain, JSON keys/values, Unicode escapes, UTF16, UNC, nested ZIP; relative paths accepted')
if __name__=='__main__':main()
