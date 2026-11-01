window.NitLinkShortcutEditor=(()=>{
  let post,t,definitions=[],saved=[],draft=[],recording=-1,armed=false,pending=null,saving=false,notice='';
  const rows=new Map(),el=id=>document.getElementById(id);
  const same=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
  const defaults=()=>definitions.map(item=>({key:item.defaultKey,modifiers:item.defaultModifiers}));
  const keyLabel=key=>{
    if(key>=48&&key<=57||key>=65&&key<=90)return String.fromCharCode(key);
    if(key>=112&&key<=135)return 'F'+(key-111);
    if(key>=96&&key<=105)return 'Num '+(key-96);
    return ({8:'Backspace',9:'Tab',13:'Enter',19:'Pause',32:'Space',33:'Page Up',34:'Page Down',35:'End',36:'Home',37:'←',38:'↑',39:'→',40:'↓',44:'Print Screen',45:'Insert',46:'Delete',106:'Num *',107:'Num +',108:'Num ,',109:'Num −',110:'Num .',111:'Num /',186:';',187:'=',188:',',189:'−',190:'.',191:'/',192:'`',219:'[',220:'\\',221:']',222:"'",223:'OEM 8',226:'OEM 102'})[key]||String(key);
  };
  const chordLabel=chord=>[chord.modifiers&1?'Ctrl':'',chord.modifiers&2?'Alt':'',chord.modifiers&4?'Shift':'',keyLabel(chord.key)].filter(Boolean).join('+');
  const sendContext=()=>post('setShortcutContext',recording>=0?2:document.activeElement?.matches('input,textarea,select,[contenteditable="true"]')?1:0);
  const cancel=()=>{if(recording<0)return;recording=-1;armed=false;pending=null;sendContext();render();};
  const render=()=>{
    if(!t)return;
    const conflicts=new Set(),seen=new Map();
    draft.forEach((chord,index)=>{const key=chord.key+':'+chord.modifiers;if(seen.has(key)){conflicts.add(index);conflicts.add(seen.get(key));}else seen.set(key,index);});
    const dirty=!same(draft,saved);
    for(const [index,row] of rows){
      const item=definitions[index];
      row.label.textContent=t(item.label);
      row.detail.textContent=[item.detail?t(item.detail):'',item.largeStep?t('shortcut.largeStep'):''].filter(Boolean).join(' · ');
      row.button.textContent=index===recording?(pending?chordLabel(pending):t(armed?'shortcut.recording':'shortcut.preparing')):chordLabel(draft[index]);
      row.button.title=t('shortcut.recordHelp');row.button.setAttribute('aria-pressed',String(index===recording));
      row.button.setAttribute('aria-label',[row.label.textContent,row.detail.textContent,row.button.textContent].filter(Boolean).join(' · '));
      row.button.dataset.conflict=String(conflicts.has(index));row.button.disabled=saving;
    }
    el('shortcut-save').disabled=!definitions.length||!dirty||conflicts.size>0||saving||recording>=0;
    el('shortcut-reset').disabled=!definitions.length||same(draft,defaults())||saving||recording>=0;
    el('shortcut-status').textContent=conflicts.size?t('shortcut.conflict'):notice?t('shortcut.'+notice):dirty?t('shortcut.unsaved'):'';
    el('shortcut-status').hidden=!el('shortcut-status').textContent;
    el('shortcut-status').dataset.error=String(conflicts.size>0||notice==='saveFailed'||notice==='unsupported');
  };
  return {
    init(options){
      ({post,t}=options);
      el('shortcut-save').addEventListener('click',()=>{saving=true;notice='';render();post('saveShortcuts',draft.map((chord,index)=>`${definitions[index].id}:${chord.key},${chord.modifiers}`).join(';'));});
      el('shortcut-reset').addEventListener('click',()=>{draft=defaults();notice='';render();});
      document.addEventListener('focusin',()=>queueMicrotask(sendContext));
      document.addEventListener('focusout',()=>queueMicrotask(sendContext));
      document.addEventListener('pointerdown',event=>{if(recording>=0&&event.target.closest('.shortcut-key')!==rows.get(recording)?.button)cancel();},true);
      window.addEventListener('blur',cancel);
      document.addEventListener('visibilitychange',()=>{if(document.hidden)cancel();});
      document.addEventListener('keydown',event=>{
        if(recording<0)return;
        event.preventDefault();event.stopImmediatePropagation();
        if(event.key==='Escape'){cancel();return;}
        if(!armed||event.repeat||event.isComposing||['Control','Alt','Shift','Meta'].includes(event.key))return;
        const key=event.keyCode,modifiers=(event.ctrlKey?1:0)|(event.altKey?2:0)|(event.shiftKey?4:0);
        const supported=[8,9,13,19,32,44,45,46,226].includes(key)||(key>=33&&key<=40)||(key>=48&&key<=57)||(key>=65&&key<=90)||(key>=96&&key<=111)||(key>=112&&key<=135)||(key>=186&&key<=192)||(key>=219&&key<=223);
        if(!supported||event.metaKey||((modifiers&2)&&(key===9||key===115))||((modifiers&3)===3&&key===46)){notice='unsupported';render();return;}
        pending={key,modifiers};notice='';render();
      },true);
      document.addEventListener('keyup',event=>{
        if(recording<0)return;
        event.preventDefault();event.stopImmediatePropagation();
        if(pending&&event.keyCode===pending.key){draft[recording]=pending;recording=-1;armed=false;pending=null;notice='';sendContext();render();}
      },true);
      render();
    },
    update(value){
      if(!value)return;
      const dirty=!same(draft,saved);definitions=value;saved=value.map(item=>({key:item.key,modifiers:item.modifiers}));
      if(!dirty)draft=saved.map(item=>({...item}));
      if(!rows.size)definitions.forEach((item,index)=>{
        const row=document.createElement('div');row.className='shortcut-row';
        const caption=document.createElement('div'),label=document.createElement('span'),detail=document.createElement('small'),button=document.createElement('button');
        caption.className='shortcut-caption';detail.className='shortcut-detail';caption.append(label,detail);
        button.type='button';button.className='shortcut-key';button.addEventListener('click',()=>{recording=index;armed=false;pending=null;notice='';sendContext();render();});
        row.append(caption,button);el(['shortcut-general','shortcut-pip','shortcut-diagnostics'][item.group]).append(row);rows.set(index,{label,detail,button});
      });
      render();
    },
    message(value){
      if(value.shortcutContext===2&&recording>=0){armed=true;render();}
      if(value.shortcutCaptureCancelled)cancel();
      if(value.shortcutNotice){saving=false;notice=value.shortcutNotice;render();}
    },
    render
  };
})();
