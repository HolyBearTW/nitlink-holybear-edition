(() => {
  const tabList = document.getElementById('view-tabs');
  if (tabList) {
    const tabs = Array.from(tabList.querySelectorAll('[role="tab"]'));
    const activate = (tab, moveFocus = false) => {
      tabs.forEach((item) => {
        const selected = item === tab;
        item.setAttribute('aria-selected', String(selected));
        item.tabIndex = selected ? 0 : -1;
        document.getElementById(item.getAttribute('aria-controls')).hidden = !selected;
      });
      if (moveFocus) {
        tab.focus();
        tab.scrollIntoView({ block: 'nearest', inline: 'nearest' });
      }
    };
    tabList.hidden = false;
    tabList.parentElement.classList.add('js-tabs');
    activate(tabs[0]);
    tabs.forEach((tab, index) => {
      tab.addEventListener('click', () => activate(tab));
      tab.addEventListener('keydown', (event) => {
        let next;
        if (event.key === 'ArrowRight') next = (index + 1) % tabs.length;
        else if (event.key === 'ArrowLeft') next = (index - 1 + tabs.length) % tabs.length;
        else if (event.key === 'Home') next = 0;
        else if (event.key === 'End') next = tabs.length - 1;
        else return;
        event.preventDefault();
        activate(tabs[next], true);
      });
    });
  }
  const trigger = document.querySelector('.video-trigger');
  if (trigger) {
    trigger.addEventListener('click', (event) => {
      if (event.metaKey || event.ctrlKey || event.shiftKey || event.altKey || event.button !== 0) return;
      event.preventDefault();
      const frame = document.createElement('iframe');
      frame.src = 'https://www.youtube-nocookie.com/embed/w8Rq5Bonbgg?autoplay=1';
      frame.title = 'NitLink 1.1 walkthrough';
      frame.allow = 'autoplay; encrypted-media; picture-in-picture; fullscreen';
      frame.allowFullscreen = true;
      frame.referrerPolicy = 'strict-origin-when-cross-origin';
      document.getElementById('showcase-player').replaceChildren(frame);
      frame.focus();
    });
  }
})();
