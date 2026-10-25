/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Exercise selection transactions/races without opening native file dialogs. Real
 * WebDriver selections/downloads complement these deliberately malformed boundaries. */
async function verifyFileTransferUI() {
    const box=document.createElement('section');document.body.appendChild(box);
    const states=[], panel=createPas2WasmFiles(box,value=>states.push(value));
    const picker=box.querySelector('input'), tick=()=>new Promise(resolve=>setTimeout(resolve,10));
    const read=()=>box.querySelector('#file-status').textContent;
    function check(value,message){if(!value)throw Error(message);}
    function choose(files){
        Object.defineProperty(picker,'files',{configurable:true,value:files});
        picker.dispatchEvent(new Event('change'));
    }
    let reads=0;
    function file(name,text='',size){
        const bytes=new TextEncoder().encode(text);
        return {name,size:size??bytes.length,arrayBuffer:async()=>{reads++;return bytes.buffer;}};
    }
    try{
        for(const [files,code] of [
            [[file('huge','',1048577)],'R_FILE_LIMIT'],
            [Array.from({length:33},(_,i)=>file('f'+i)),'R_FILE_LIMIT'],
            [[file('same'),file('same')],'R_FILE_NAME'],
            [[file('')],'R_FILE_NAME'],[[file('a\0b')],'R_FILE_NAME'],
            [[file('é'.repeat(128))],'R_FILE_NAME'],[[file('\ud800')],'R_FILE_NAME']
        ]){
            reads=0;choose(files);await tick();
            check(read().includes(code)&&reads===0&&panel.inputs().length===0,'Metadata read or partially accepted invalid files');
        }
        choose([file('input.txt','original')]);await tick();
        check(panel.inputs().length===1,'Valid selection was rejected');
        reads=0;choose([file('input.txt','duplicate')]);await tick();
        check(read().includes('R_FILE_NAME')&&reads===0&&panel.inputs()[0].data.length===8,'Duplicate changed an existing input');
        choose([file('valid.txt','ok'),{name:'bad.txt',size:1,arrayBuffer:async()=>new Uint8Array([255]).buffer}]);await tick();
        check(read().includes('R_ENCODING')&&panel.inputs().length===1,'Encoding rejection was not atomic');
        choose([{name:'unreadable',size:0,arrayBuffer:async()=>{throw new DOMException('Cannot read file','NotReadableError');}}]);await tick();
        check(read().includes('R_FILE_STATE')&&panel.inputs().length===1&&panel.ready(),'Read failure lost inputs or left Run disabled');
        panel.reset();
        choose([file('full.txt','a'.repeat(1048576)),...Array.from({length:31},(_,i)=>file('f'+i))]);await tick();
        check(panel.inputs().length===32&&box.querySelector('#file-budget').textContent.includes('0 bytes available'),'Exact file/byte budgets failed');
        panel.busy(true);check(picker.disabled&&box.querySelector('#clear-files').disabled,'Selection remained enabled during a run');
        panel.busy(false);box.querySelector('#clear-files').click();check(panel.inputs().length===0,'Clear inputs failed');
        // Reset discards an old read without cancelling a newer accepted selection.
        let release;choose([{name:'old.txt',size:3,arrayBuffer:()=>new Promise(resolve=>release=resolve)}]);
        check(!panel.ready()&&states.at(-1)===false,'Loading did not disable Run');panel.reset();
        choose([file('new.txt','new')]);await tick();release(new TextEncoder().encode('old').buffer);await tick();
        check(panel.inputs().length===1&&panel.inputs()[0].name==='new.txt'&&panel.ready(),'Stale read returned after Reset');
        // Rows are inert, downloads are explicit, exact bytes survive caller mutation,
        // and the URL is released. No actual download is started by this boundary test.
        const originalCreate=URL.createObjectURL, originalRevoke=URL.revokeObjectURL, originalClick=HTMLAnchorElement.prototype.click;
        let created=0,revoked=0,suggested,blob;
        URL.createObjectURL=value=>{created++;blob=value;return originalCreate.call(URL,value);};
        URL.revokeObjectURL=value=>{revoked++;originalRevoke.call(URL,value);};
        HTMLAnchorElement.prototype.click=function(){suggested=this.download;};
        try{
            const bytes=new Uint8Array([239,187,191,0,13,10,8]);
            panel.results([{name:'../<img>\n.txt',data:bytes}]);bytes.fill(65);
            check(!box.querySelector('img')&&created===0,'Filename became markup or download started automatically');
            box.querySelector('#output-file-list button').click();
            check(created===1&&suggested==='__img__.txt','Download filename retained path/control syntax');
            check(JSON.stringify(Array.from(new Uint8Array(await blob.arrayBuffer())))==='[239,187,191,0,13,10,8]','Download changed exact result bytes');
            await new Promise(resolve=>setTimeout(resolve,1100));check(revoked===1,'Download URL was not released');
        }finally{URL.createObjectURL=originalCreate;URL.revokeObjectURL=originalRevoke;HTMLAnchorElement.prototype.click=originalClick;}
        box.querySelector('#input-file-list button').click();
        check(!panel.inputs().length&&!box.querySelector('#output-file-list button'),'Removing input retained stale results');
        return true;
    }finally{panel.reset();box.remove();}
}
