// Localized titles come from platform/catalogue responses, never a per-game translation table.
window.NitLinkGameTitles = (() => {
  const catalogue=new Map(),families=new Map();
  const edition=/[\s:·|—–-]*(?:[（(]\s*)?(?:Nintendo\s+)?Switch\s*2\s*(?:Edition|版|エディション|에디션)\s*[）)]?$/i;
  const base=value=>String(value||'').replace(edition,'').trim();
  const demo=/(?:^|[\s:·|—–（(-]+)(?:demo(?:\s*(?:ver\.?|version))?|trial(?:\s*version)?)[\s.）)]*$|[\s:·|—–（(-]*(?:體驗版|体验版|試玩版|试玩版|体験版|체험판)[\s）)]*$/i;
  const isDemo=game=>demo.test(base(typeof game==='string'?game:game.name));
  const coverQuery=value=>base(base(value).replace(demo,'')).trim();
  const key=value=>base(value).normalize('NFKC').replace(/[\s\p{P}\p{S}_]+/gu,'').toLowerCase();
  const cacheKey=game=>/^(nintendo|playstation|xbox)-/.test(game.key||'')?game.key:game.id||game.key;
  const remember=(games,locale)=>{
    for(const game of games){
      const id=cacheKey(game);if(!id||!game.name)continue;
      const cached=catalogue.get(id)||{name:game.name,labels:{}};
      if(game.titleLocale||game.source||game.nameLocale==='en-US')cached.name=game.name;
      Object.assign(cached.labels,game.titles||{},game.displayNames||{});
      if(!game.customName&&game.displayName)cached.labels[locale]=game.displayName;
      if(game.nameLocale)cached.labels[game.nameLocale]=game.name;
      catalogue.set(id,cached);
      const aliases=[cached.name,...Object.values(cached.labels)].map(key);
      const labels=aliases.map(alias=>families.get(alias)).find(Boolean)||{};
      for(const [language,label] of Object.entries(cached.labels)){
        if(!labels[language]||base(label)!==base(cached.name)||game.nameLocale===language)labels[language]=base(label);
      }
      for(const alias of aliases)families.set(alias,labels);
    }
  };
  const label=(game,locale,editionLabel)=>{
    if(game.customName)return game.customName;
    if(game.nameLocale===locale)return edition.test(game.name)?base(game.name)+' · '+editionLabel:game.name;
    const cached=catalogue.get(cacheKey(game)),name=cached?.name||game.name||'';
    let own=(game.nameLocale===locale?game.name:'')||game.displayNames?.[locale]||game.titles?.[locale]||((game.titleLocale===locale||game.locale===locale)?game.displayName:'')||cached?.labels[locale]||name;
    if(locale==='en-US'&&/[\u3400-\u9fff]/.test(own)&&!/[\u3400-\u9fff]/.test(name))own=name;
    const translated=families.get(key(name))?.[locale];
    if(edition.test(name)&&translated)return (base(own)===base(name)?translated:base(own))+' · '+editionLabel;
    return translated&&own===name?translated:own;
  };
  const aliases=game=>{
    const cached=catalogue.get(cacheKey(game));
    const version=isDemo(game)?'demo:':'';
    return [...new Set([game.id?'id:'+version+game.id:'',...[game.name,game.customName?'':game.displayName,...Object.values(game.displayNames||{}),...Object.values(game.titles||{}),cached?.name,...Object.values(cached?.labels||{})].filter(Boolean).map(value=>'title:'+version+key(value))].filter(Boolean))];
  };
  return {base,key,coverQuery,isDemo,remember,label,aliases,isSwitch2:game=>edition.test(game.name||'')||game.platform==='Switch 2'};
})();

// Account data comes from the native host. Tokens never appear in this view model.
(() => {
  const providers = ['nintendo', 'playstation', 'xbox'];
  const names = {nintendo: 'Nintendo', playstation: 'PlayStation', xbox: 'Xbox'};
  const systems = {nintendo: 'Switch / Switch 2', playstation: 'PS4 / PS5', xbox: 'One / Series X|S'};
  let post, t, locale, paintImage, notify, model = {accounts: [], games: []}, local = {games: [], favorites: [], recent: []};
  let sourceChoice='', defaultSourceSelected=false;
  const titles=window.NitLinkGameTitles,thumbnails=new Map();
  let current = {}, sourceProvider='', filter = 'all', query = '', sort = 'recent', dialogProvider = '', signature = '', linkTarget = '', submitted = false, linkRevision=0, lookupPending=false, manualLink=false;
  const el = id => document.getElementById(id);
  const text = (tag, value, className = '') => {
    const node = document.createElement(tag); node.textContent = value; node.className = className; return node;
  };
  const logo = provider => { const image=document.createElement('img');image.src=`assets/menu/platforms/${provider==='nintendo'?'nintendo-switch':provider}.svg`;image.alt='';image.className='account-platform-logo';return image; };
  const findCovers = scroll => {
    linkTarget=current.key;el('game-query').value=titles.coverQuery(current.name);el('game-search').click();
    document.dispatchEvent(new Event('accountcoverlookup'));
    if(scroll)el('game-query').scrollIntoView({block:'center',behavior:'smooth'});
    render();
  };
  const time = minutes => minutes == null ? t('account.noTime') :
    t('account.hoursMinutes').replace('{hours}', new Intl.NumberFormat(locale).format(Math.floor(minutes / 60))).replace('{minutes}', minutes % 60);
  const date = value => value ? new Date(typeof value === 'number' ? value * 1000 : value).toLocaleDateString(locale, {month: 'short', day: 'numeric'}) : t('account.never');
  const account = provider => model.accounts.find(item => item.provider === provider) || {provider, status: 'disconnected'};
  const chooseDefaultSource = () => {
    // Wait for the host's source setting before considering restored account data.
    if(defaultSourceSelected||!sourceChoice)return;
    if(sourceChoice!=='auto'){defaultSourceSelected=true;return;}
    let provider='',gameCount=0;
    for(const candidate of providers){
      if(!account(candidate).accountId)continue;
      const count=model.games.filter(game=>game.provider===candidate).length;
      if(count>gameCount){provider=candidate;gameCount=count;}
    }
    if(!provider)return;
    defaultSourceSelected=true;
    const platforms=model.games.filter(game=>game.provider===provider).map(game=>game.platform);
    const [source,label]={
      nintendo:platforms.includes('Switch 2')?['switch2','Switch 2']:['switch','Switch'],
      playstation:platforms.includes('PS5')?['ps5','PS5']:['ps4','PS4'],
      xbox:platforms.some(platform=>platform.includes('Series'))?['xbox_series','Xbox Series X|S']:['xbox_one','Xbox One']
    }[provider];
    post('setManualHdmiSource',source);
    notify(t('account.sourceSelected').replace('{source}',label));
  };
  const rows = () => {
    const merged = new Map(local.games.map(game => [game.key, {...game, provider:'', totalMinutes:null, lastPlayed:game.lastUsedUtc||0}]));
    for(const game of model.games){const saved=merged.get(game.key);merged.set(game.key,{...saved,...game,image:saved?.image||game.image,favorite:!!saved?.favorite,displayNames:saved?.displayNames||game.displayNames});}
    const groups=[];
    for(const game of merged.values()){
      const aliases=titles.aliases(game);let group=groups.find(group=>group.aliases.some(alias=>aliases.includes(alias)));
      if(!group){group={aliases:[],variants:[]};groups.push(group);}group.aliases.push(...aliases);group.variants.push(game);
    }
    return groups.map(group=>{
      const owned=group.variants.filter(game=>game.provider),primary=owned[0]||group.variants.find(game=>!titles.isSwitch2(game))||group.variants[0];
      const active=group.variants.find(game=>game.key===current.key)||primary;
      const activity=active.provider?active:primary;
      const platforms=[...new Set(group.variants.map(game=>titles.isSwitch2(game)?'Switch 2':game.platform).filter(platform=>platform&&platform!=='NitLink'))].sort();
      const preview=thumbnails.get(active.id)||thumbnails.get(primary.id)||group.variants.map(game=>thumbnails.get(game.id)).find(Boolean);
      return {...active,provider:activity.provider,totalMinutes:activity.totalMinutes,lastPlayed:activity.lastPlayed,displayName:active.customName||primary.customName||titles.base(titles.label(primary,locale,t('account.switch2Edition'))),image:active.manualCover?active.image:preview||active.image||primary.image,
        platform:platforms.join(' / '),platformProvider:active.provider||(titles.isSwitch2(active)?'nintendo':primary.provider),favorite:group.variants.some(game=>game.favorite),current:group.variants.some(game=>game.key===current.key),providers:[...new Set(owned.map(game=>game.provider))],variants:group.variants};
    });
  };
  const showDialog = provider => {
    dialogProvider = provider; submitted = false;manualLink=false;
    el('account-link-code').value = '';
    el('account-link-dialog').returnValue='';
    renderDialog();
    el('account-link-dialog').showModal();
  };
  const renderDialog = () => {
    const state = account(dialogProvider), isXbox = dialogProvider === 'xbox';
    el('account-link-title').replaceChildren(logo(dialogProvider),document.createTextNode(t('account.linkTitle').replace('{provider}', names[dialogProvider])));
    el('account-link-help').textContent = t(model.preview ? 'account.previewHelp' : `account.help.${dialogProvider}${manualLink?'Manual':''}`);
    el('account-link-code').disabled = model.preview === true;
    el('account-link-submit').textContent=t(model.preview?'account.previewFinish':'account.finishLink');
    el('account-disconnect').hidden = !state.accountId;
    el('account-link-code-label').hidden=!manualLink;el('account-link-code').hidden=!manualLink;el('account-link-submit').hidden=!manualLink;
    el('account-login-manual').hidden=isXbox||!!model.preview;
    el('account-login-manual').textContent=t(manualLink?'account.automaticLink':'account.manualLink');
    el('account-login-manual').disabled=state.status==='loading';
    el('account-link-cancel').hidden=state.status!=='awaiting'||!!model.preview;
    el('account-link-code-label').textContent = t(dialogProvider === 'playstation' ? 'account.npssoLabel' : 'account.callbackLabel');
    el('account-link-code').placeholder = t(dialogProvider === 'playstation' ? 'account.npssoPlaceholder' : 'account.callbackPlaceholder');
    el('account-link-code').type = dialogProvider === 'playstation' ? 'password' : 'text';
    el('account-open-cookie').hidden = dialogProvider !== 'playstation'||!manualLink||model.preview===true;
    el('account-link-submit').disabled = state.status === 'loading' || (model.preview !== true && !el('account-link-code').value.trim());
    if(model.preview)el('account-link-open').href={nintendo:'https://accounts.nintendo.com/login',playstation:'https://www.playstation.com/acct/management/',xbox:'https://login.live.com/'}[dialogProvider];
    el('account-link-open').disabled = state.status === 'loading';
    el('account-link-feedback').textContent = state.error ? t(`account.error.${state.error}`) : model.preview ? t('account.preview') : state.status==='loading'?t('account.syncing'):state.status === 'awaiting' ? t(manualLink?(dialogProvider==='playstation'?'account.awaitingPsn':'account.awaiting'):isXbox?'account.awaitingXbox':'account.awaitingAutomatic') : '';
    el('account-link-feedback').dataset.error = String(!!state.error);
  };
  const render = () => {
    if (!post) return;
    el('account-preview-note').hidden = model.preview !== true;
    const connectedCount = providers.filter(provider => account(provider).accountId).length;
    const unlinked = el('account-unlinked');
    const hideUnlinked = connectedCount === 0 || connectedCount === providers.length;
    if (unlinked.hidden && !hideUnlinked) unlinked.open = false;
    unlinked.hidden = hideUnlinked;
    for (const provider of providers) {
      const state = account(provider), card = el(`account-${provider}`), connected = !!state.accountId;
      const grid = el(connected || connectedCount === 0 ? 'account-linked-grid' : 'account-unlinked-grid');
      if (card.parentElement !== grid) grid.append(card);
      card.dataset.status = state.status;
      card.querySelector('.account-status').textContent = t(`account.status.${state.status}`);
      card.querySelector('.account-nickname').textContent = connected ? state.nickname : systems[provider];
      const count = model.games.filter(game => game.provider === provider).length;
      card.querySelector('.account-summary').textContent = connected ? t('account.summary').replace('{count}', count).replace('{date}', date(state.syncedAt)) : t(`account.description.${provider}`);
      const link = card.querySelector('[data-link]');
      link.textContent = t(connected ? 'account.manage' : 'account.connect');
      card.querySelector('[data-sync]').hidden = !connected;
      card.querySelector('[data-sync]').disabled = state.status === 'loading';
      card.querySelector('[data-sync]').textContent = t(state.status === 'loading' ? 'account.syncing' : 'account.sync');
      const error = card.querySelector('.account-error'); error.hidden = !state.error; error.textContent = state.error ? t(`account.error.${state.error}`) : '';
    }
    const all = rows();
    const selected = all.find(game => game.current);
    const missing=sourceProvider?(account(sourceProvider).accountId?[]:[sourceProvider]):providers.filter(provider=>!account(provider).accountId);
    for(const prompt of document.querySelectorAll('[data-account-cover-prompt]')){
      prompt.hidden=missing.length===0;prompt.querySelector('span').textContent=t('account.moreCovers');
      const actions=prompt.querySelector('div');actions.replaceChildren();
      for(const provider of missing){const button=text('button',names[provider],'game-button');button.type='button';button.prepend(logo(provider));button.addEventListener('click',()=>showDialog(provider));actions.append(button);}
    }
    const stats = el('account-current-stats'); stats.hidden = !selected?.provider;
    stats.replaceChildren();
    if (selected?.provider) {
      const badge=text('span', names[selected.provider], 'account-platform-pill');badge.prepend(logo(selected.provider));
      stats.append(badge, text('strong', time(selected.totalMinutes)), text('span', t('account.lastPlayed').replace('{date}', date(selected.lastPlayed))));
    }
    el('account-browser-count').textContent = t('account.gameCount').replace('{count}', all.length);
    const filtered = all.filter(game => {
      if (filter === 'favorites' && !game.favorite) return false;
      if (filter === 'recent' && !game.lastPlayed) return false;
      if (providers.includes(filter) && !game.providers.includes(filter)) return false;
      return !query || `${game.displayName || game.name} ${game.name} ${game.platform || ''}`.toLocaleLowerCase(locale).includes(query);
    });
    filtered.sort((a, b) => Number(titles.isDemo(a))-Number(titles.isDemo(b)) || (sort === 'time' ? (b.totalMinutes ?? -1) - (a.totalMinutes ?? -1) : sort === 'name' ? (a.displayName || a.name).localeCompare(b.displayName || b.name, locale) : new Date(typeof b.lastPlayed === 'number' ? b.lastPlayed * 1000 : b.lastPlayed || 0) - new Date(typeof a.lastPlayed === 'number' ? a.lastPlayed * 1000 : a.lastPlayed || 0)));
    el('account-match-game').hidden = !selected?.provider;
    el('account-match-note').hidden = !linkTarget;
    for (const button of el('account-source-tabs').querySelectorAll('button')) button.setAttribute('aria-pressed', String(button.dataset.accountFilter === filter));
    const listSignature = JSON.stringify([filtered, current.key, locale]);
    if (signature !== listSignature) {
      signature = listSignature;
      const list = el('account-game-list'),scrollTop=list.scrollTop;
      const artwork=new Map([...list.querySelectorAll('.account-game-card')].map(card=>[card.dataset.key,card.querySelector('.account-game-art')]));
      const fragment=document.createDocumentFragment();
      for (const game of filtered) {
        const card = document.createElement('article'); card.className = 'account-game-card'; card.dataset.key=game.key;card.dataset.provider = game.platformProvider||game.provider; card.dataset.current = String(game.current);
        const select = document.createElement('button'); select.type = 'button'; select.className = 'account-game-select'; select.setAttribute('aria-pressed', String(game.current));
        const art = artwork.get(game.key)||text('div', '', 'account-game-art game-art game-search-art');
        paintImage(art,game.image);
        const info = text('div', '', 'account-game-info');
        const platform=text('span', game.platform || t('account.local'), 'account-game-platform');if(game.platformProvider)platform.prepend(logo(game.platformProvider));
        info.append(platform, text('strong', game.displayName || game.name, 'account-game-title'), text('span', game.provider ? time(game.totalMinutes) : t('account.localGame'), 'account-game-time'), text('span', t('account.lastPlayed').replace('{date}', date(game.lastPlayed)), 'account-game-date'));
        select.append(art, info); select.addEventListener('click', () => {const retry=current.key===game.key&&!current.id;post('setGame', game.key);if(retry)findCovers(false);});
        const favorite = text('button', game.favorite ? '★' : '☆', 'account-game-favorite'); favorite.type = 'button'; favorite.setAttribute('aria-label', t(game.favorite ? 'game.removeFavorite' : 'game.addFavorite')); favorite.setAttribute('aria-pressed', String(!!game.favorite));
        favorite.addEventListener('click',()=>{if(game.favorite){for(const variant of game.variants)if(variant.favorite)post('removeGameFavorite',variant.key);}else post('addGameFavorite',game.key);});
        card.append(select,favorite);
        fragment.append(card);
      }
      list.replaceChildren(fragment);list.scrollTop=scrollTop;
    }
    el('account-browser-empty').hidden = filtered.length > 0;
    el('account-browser-empty').textContent = t(query ? 'account.noResults' : providers.includes(filter) && !account(filter).accountId ? 'account.connectToBrowse' : 'account.empty');
    el('account-browser-footnote').textContent = t(filter === 'nintendo' ? 'account.nintendoDelay' : filter === 'xbox' ? 'account.xboxCoverage' : 'account.timeNote');
    if (el('account-link-dialog').open) renderDialog();
  };
  window.NitLinkAccountLibrary = {
    init(options) {
      ({post, t, image:paintImage, notify} = options); locale = options.locale;
      for (const provider of providers) {
        const card = el(`account-${provider}`);
        card.querySelector('[data-link]').addEventListener('click', () => showDialog(provider));
        card.querySelector('[data-sync]').addEventListener('click', () => post('syncAccount', provider));
      }
      el('account-source-tabs').addEventListener('click', event => { const button = event.target.closest('[data-account-filter]'); if (!button) return; filter = button.dataset.accountFilter; render(); });
      el('account-game-query').addEventListener('input', event => { query = event.target.value.trim().toLocaleLowerCase(locale); render(); });
      el('account-game-sort').addEventListener('change', event => { sort = event.target.value; render(); });
      el('account-link-close').addEventListener('click', () => el('account-link-dialog').close());
      el('account-link-dialog').addEventListener('close', () => { el('account-link-code').value = '';if(el('account-link-dialog').returnValue!=='linked'&&(submitted||account(dialogProvider).status==='awaiting')&&!model.preview){if(dialogProvider==='xbox')post('cancelXboxLogin');else post('cancelNativeAccountLink',dialogProvider);}submitted=false; });
      el('account-link-code').addEventListener('input', renderDialog);
      if(window.NitLinkPreview){
        const old=el('account-link-open'),link=document.createElement('a');
        link.id=old.id;link.className=old.className;link.textContent=old.textContent||t('account.openLogin');link.dataset.i18n=old.dataset.i18n;old.replaceWith(link);
      }
      el('account-link-open').addEventListener('click', () => {
        if(model.preview)return;
        linkRevision=account(dialogProvider).linkRevision||0;submitted=!manualLink;
        post(manualLink?'beginManualAccountLink':'beginAccountLink',dialogProvider);
      });
      el('account-login-manual').addEventListener('click',()=>{manualLink=!manualLink;el('account-link-code').value='';linkRevision=account(dialogProvider).linkRevision||0;submitted=!manualLink;renderDialog();post(manualLink?'beginManualAccountLink':'beginAccountLink',dialogProvider);});
      el('account-link-cancel').addEventListener('click',()=>{submitted=false;if(dialogProvider==='xbox')post('cancelXboxLogin');else post('cancelNativeAccountLink',dialogProvider);});
      el('account-open-cookie').addEventListener('click', () => post('openPsnCookie'));
      el('account-match-game').addEventListener('click', () => findCovers(true));
      el('account-link-submit').addEventListener('click', () => {
        const credential = el('account-link-code').value.trim(); el('account-link-code').value = '';
        linkRevision=account(dialogProvider).linkRevision||0;submitted = true; post('finishAccountLink', {provider: dialogProvider, credential});
      });
      el('account-disconnect').addEventListener('click', () => { post('disconnectAccount', dialogProvider); el('account-link-dialog').close(); });
      render();
    },
    setLocale(value) { locale = value; render(); },
    updateSource(value) {
      const source=String(value.manualHdmiSource||'auto');
      sourceChoice=source;
      const key=(source==='auto'?value.effectiveHdmiSource||'':source).replace(/[^a-z0-9]/gi,'').toLowerCase();
      const next=/^(?:nintendo)?switch2?$/.test(key)?'nintendo':/^(?:ps[45](?:pro)?|playstation[45])$/.test(key)?'playstation':key.startsWith('xbox')?'xbox':'';
      if(next!==sourceProvider){sourceProvider=next;render();}
      chooseDefaultSource();
    },
    update(value) {
      const sourceKey=value=>JSON.stringify(value.games.map(game=>[game.key,game.name,game.nameLocale,game.displayNames,game.artworks.map(source=>[source.url,source.language])]));
      const changed=sourceKey(model)!==sourceKey(value);
      model=value;titles.remember(value.games,locale);
      chooseDefaultSource();
      if (submitted && el('account-link-dialog').open && account(dialogProvider).linkRevision>linkRevision && account(dialogProvider).status === 'connected' && !account(dialogProvider).error) {submitted=false;el('account-link-dialog').close('linked');}
      el('account-disconnect').hidden = !account(dialogProvider).accountId;
      render();
      if(changed&&current.key&&!linkTarget)document.dispatchEvent(new Event('accountcoverschanged'));
    },
    updateLocal(value) { local=value;titles.remember(value.games,value.locale||locale);render(); },
    catalogue(games) { titles.remember(games,locale);render(); },
    thumbnail(event) {if(event.image){thumbnails.set(event.gameId,event.image);render();}},
    searchGames(value) {
      const needle=titles.key(titles.coverQuery(value));
      if(!needle)return [];
      return rows().filter(game=>game.provider&&titles.aliases(game).some(alias=>alias.startsWith('title:')&&alias.slice(6).includes(needle)))
        .sort((a,b)=>Number(titles.isDemo(a))-Number(titles.isDemo(b)))
        .map(game=>({...game,id:0,accountKey:game.key,source:game.provider+'-account',titleLocale:locale,imageStatus:game.image?'ok':game.artworks?.length?'loading':'missing',coverShape:'square',coverIsOfficial:true}));
    },
    updateCurrent(value) { const changed=current.key!==value.key;if(changed){linkTarget='';lookupPending=!value.id&&/^(nintendo|playstation|xbox)-/.test(value.key);}current = value;render(); },
    lookupCurrent(community=false) {
      if(current.id||!/^(nintendo|playstation|xbox)-/.test(current.key||'')||(!lookupPending&&!community))return false;
      lookupPending=false;
      if(!community&&model.games.find(game=>game.key===current.key)?.artworks.length)return false;
      findCovers(false);return true;
    },
    searchResults(games,status='ok') {
      if(!linkTarget)return;
      if(games.length&&games.every(game=>game.accountKey)){linkTarget='';render();document.dispatchEvent(new CustomEvent('accountcoverschanged',{detail:{lookupStatus:['ok','partial'].includes(status)?'empty':status}}));return;}
      const normalize=value=>titles.key(titles.coverQuery(value));
      const selected=model.games.find(game=>game.key===current.key),saved=local.games.find(game=>game.key===current.key);
      const targets=[current.name,selected?.name,...Object.values(selected?.displayNames||{}),...Object.values(saved?.displayNames||{})].map(normalize).filter(Boolean);
      const matches=games.filter(game=>[game.name,game.displayName,...Object.values(game.titles||{}),...(game.artworkSources||[]).map(source=>source.productTitle)].some(name=>targets.includes(normalize(name))));
      if(matches.length===1)this.linkSearchResult(matches[0].id);
      else document.dispatchEvent(new CustomEvent('accountcoverschanged',{detail:{lookupStatus:games.length?'matchRequired':['ok','partial'].includes(status)?'empty':status}}));
    },
    linkSearchResult(gameId) { if(!linkTarget||!gameId)return false;post('linkAccountGame',{key:linkTarget,gameId});linkTarget='';render();return true; }
  };
})();
