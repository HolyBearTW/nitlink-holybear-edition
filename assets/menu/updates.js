// Update UI receives validated native release/transfer state; downloads stay native.
window.NitLinkUpdates = {
  create({ t, post, locale }) {
    const card = document.querySelector('.about-update');
    const button = document.getElementById('holy-bear-check-update');
    const cancel = document.getElementById('holy-bear-cancel-download');
    const notes = document.getElementById('holy-bear-release-notes');
    const progress = document.getElementById('holy-bear-download-progress');
    let action = 'checkHolyBearUpdate';
    let renderedNotes;
    button.addEventListener('click', () => {
      button.disabled = true;
      button.setAttribute('aria-busy', 'true');
      if (action === 'checkHolyBearUpdate') {
        const status = document.getElementById('holy-bear-update-status');
        status.textContent = t('update.checking'); status.hidden = false;
      }
      post(action);
    });
    cancel.addEventListener('click', () => { cancel.disabled = true; post('cancelHolyBearUpdateDownload'); });
    return state => {
      const version = (state.current || '').replace(/^v/, '').replace(/-holybear$/, '');
      const latest = (state.latest || '').replace(/^v/, '').replace(/-holybear$/, '');
      const replace = text => text.replace('{version}', latest);
      const bytes = amount => new Intl.NumberFormat(locale(), { maximumFractionDigits: 1 }).format(amount / 1048576) + ' MB';
      document.getElementById('holy-bear-product-title').textContent = t('about.productTitle').replace('{version}', version).replace(/\s{2,}/g, ' ').trim();
      const transfer = state.transferStatus || 'idle';
      const downloading = transfer === 'downloading';
      const ready = transfer === 'ready' || transfer === 'launchError';
      const busy = !!state.busy || downloading || transfer === 'verifying' || transfer === 'installing';
      action = ready ? 'installHolyBearUpdate' : state.hasNewer && state.canDownload ? 'downloadHolyBearUpdate' : 'checkHolyBearUpdate';
      const error = ['downloadError', 'diskError', 'corrupt', 'launchError'].includes(transfer);
      const buttonKey = state.busy ? 'checking' : downloading ? 'downloadingButton' : transfer === 'verifying' ? 'verifying' :
        transfer === 'installing' ? 'installing' : ready ? 'install' : error && state.canDownload ? 'retry' :
        state.hasNewer && state.canDownload ? 'download' : 'check';
      button.textContent = t('update.' + buttonKey);
      button.disabled = busy;
      button.classList.toggle('is-accent', !busy && action !== 'checkHolyBearUpdate');
      button.setAttribute('aria-busy', busy ? 'true' : 'false');
      const status = document.getElementById('holy-bear-update-status');
      let key = state.status || 'never';
      if (transfer !== 'idle') key = transfer;
      if (key === 'available' && state.notes && !state.canDownload) key = 'noInstaller';
      let text = replace(t('update.' + key));
      if (key === 'available' && state.packageBytes) {
        const date = new Date(state.publishedAt);
        text += ' · ' + bytes(state.packageBytes);
        if (!Number.isNaN(date.getTime())) text += ' · ' + date.toLocaleDateString(locale());
      }
      const percent = state.packageBytes ? Math.min(100, Math.floor(100 * (state.receivedBytes || 0) / state.packageBytes)) : 0;
      if (downloading) text = text.replace('{percent}', percent).replace('{received}', bytes(state.receivedBytes || 0)).replace('{total}', bytes(state.packageBytes || 0));
      status.textContent = text;
      status.hidden = key === 'never';
      status.classList.toggle('is-new-version', state.hasNewer === true);
      status.classList.toggle('is-error', error);
      progress.hidden = !downloading && transfer !== 'verifying';
      progress.value = percent;
      progress.setAttribute('aria-label', t('update.downloadingButton'));
      cancel.hidden = !downloading;
      cancel.disabled = false;
      cancel.textContent = t('update.cancelDownload');
      card.querySelector('.about-download').hidden = progress.hidden && cancel.hidden;
      if (renderedNotes !== state.notes) {
        renderedNotes = state.notes;
        // Release Markdown is converted, then sanitized once at the HTML boundary.
        const fragment = DOMPurify.sanitize(marked.parse(state.notes || '', { gfm: true, breaks: false }), {
          ALLOWED_TAGS: ['h1','h2','h3','h4','h5','h6','p','ul','ol','li','strong','em','del','code','pre','blockquote','a','hr','br','table','thead','tbody','tr','th','td'],
          ALLOWED_ATTR: ['href','title','start'], ALLOW_DATA_ATTR: false,
          ALLOWED_URI_REGEXP: /^https:\/\//i, RETURN_DOM_FRAGMENT: true
        });
        fragment.querySelectorAll('a').forEach(link => { link.target = '_blank'; link.rel = 'noopener noreferrer'; });
        notes.replaceChildren(fragment);
      }
      notes.hidden = !state.notes;
      const lastCheck = state.lastCheckUtc ? new Date(state.lastCheckUtc * 1000) : null;
      document.getElementById('holy-bear-last-check').textContent = lastCheck && state.lastCheckUtc > 0 && !Number.isNaN(lastCheck.getTime()) ? lastCheck.toLocaleString(locale()) : t('update.never');
      const toggle = document.getElementById('toggle-holy-bear-auto-update');
      toggle.classList.toggle('on', !!state.automatic);
      toggle.setAttribute('aria-checked', state.automatic ? 'true' : 'false');
      document.getElementById('holy-bear-update-badge').hidden = !state.automatic || !state.unread;
      card.dataset.transfer = transfer;
    };
  }
};
