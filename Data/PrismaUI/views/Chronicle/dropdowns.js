/* Themed drop-down lists: one list for every choice in DovahHub (DHUB-134).

   The game UI draws a <select>'s list as a plain system pop-up, and on a pad A opened nothing: it started an invisible
   edit in which every Left or Right step changed the value and saved it. So every single-choice <select> opens this list
   instead, by mouse, keyboard or pad, and the person ranks (DHUB-128) use it as a button that opens a list. The list is
   made of buttons: Up and Down (arrows, the D-pad) move in it, a click, Enter or A chooses, and Escape, Tab, B, a press
   outside or the page moving close it with nothing changed. A <select> keeps its value, labels and change events, and
   gets one change when a different option is chosen. A closed <select> takes no arrow key or typed letter, so the
   value only ever changes by a choice in the open list, and the highlight moves past it like past any control.

   Add-on pages shown inside DovahHub use the same list (window.DovahHubChoices, below) instead of a native <select>.
   Text goes in with textContent. */
(function () {
  'use strict';
  let list = null;  // the open list: { menu, owner, dispose, onClose }

  function setText(element, text) {
    if (window.DovahHubI18n) DovahHubI18n.setText(element, text);
    else element.textContent = text;
  }
  function setLabel(element, text) {
    if (window.DovahHubI18n) DovahHubI18n.setAttribute(element, 'aria-label', text);
    else element.setAttribute('aria-label', text);
  }
  // The highlight goes back to an element the way DovahHub moves it: focus, and focusin said outright for the pad.
  function highlight(element) {
    element.focus({ preventScroll: true });
    element.dispatchEvent(new FocusEvent('focusin', { bubbles: true }));
    element.scrollIntoView({ block: 'nearest', inline: 'nearest' });
  }
  function options() {
    return list ? Array.from(list.menu.children).filter(function (row) { return !row.hidden && !row.disabled; }) : [];
  }

  // chosen: whether a different choice closed it (onChoose runs right after).
  function close(refocus, chosen) {
    if (!list) return;
    const current = list;
    list = null;
    if (current.dispose) current.dispose();
    current.menu.remove();
    current.owner.classList.remove('is-open');
    current.owner.setAttribute('aria-expanded', 'false');
    if (refocus && document.contains(current.owner)) highlight(current.owner);
    if (typeof current.onClose === 'function') {
      try { current.onClose({ chosen: !!chosen }); } catch (error) { if (window.console) console.error('DovahHubChoices onClose', error); }
    }
  }

  function place(menu, owner) {
    const rect = owner.getBoundingClientRect();
    const gap = 2, margin = 8;
    menu.style.minWidth = rect.width + 'px';
    menu.style.left = Math.max(margin, Math.min(rect.left, window.innerWidth - menu.offsetWidth - margin)) + 'px';
    const below = window.innerHeight - rect.bottom - margin;
    const above = rect.top - margin;
    const height = menu.scrollHeight;
    if (height <= below || below >= above) {
      menu.style.maxHeight = Math.max(120, below - gap) + 'px';
      menu.style.top = (rect.bottom + gap) + 'px';
    } else {
      menu.style.maxHeight = Math.max(120, above - gap) + 'px';
      menu.style.top = Math.max(margin, rect.top - gap - Math.min(height, above - gap)) + 'px';
    }
  }

  // choices: [{ value, label, disabled, hidden, data }] (data: extra data-* on the option). onChoose(value, choice) runs
  // only for a choice other than current. settings: { className, label, refocus, onClose } (refocus false: the caller
  // moves the highlight itself after choosing; onClose({ chosen }) runs once whenever the list closes, however it closes,
  // before onChoose when a different choice closed it).
  function open(owner, choices, current, onChoose, settings) {
    close(false);
    settings = settings || {};
    const menu = document.createElement('div');
    menu.className = 'wf-select-menu' + (settings.className ? ' ' + settings.className : '');
    menu.setAttribute('role', 'listbox');
    const label = settings.label || owner.getAttribute('aria-label');
    if (label) setLabel(menu, label);
    let start = null;
    choices.forEach(function (choice) {
      const chosen = String(choice.value) === String(current);
      const option = document.createElement('button');
      option.type = 'button';
      option.className = 'wf-select-option' + (chosen ? ' is-selected' : '') + (choice.disabled ? ' is-disabled' : '');
      option.setAttribute('role', 'option');
      option.setAttribute('aria-selected', chosen ? 'true' : 'false');
      option.dataset.value = String(choice.value);
      Object.keys(choice.data || {}).forEach(function (key) { option.dataset[key] = String(choice.data[key]); });
      option.disabled = !!choice.disabled;
      option.hidden = !!choice.hidden;
      setText(option, String(choice.label === undefined ? choice.value : choice.label));
      option.addEventListener('mousemove', function () {
        if (document.activeElement !== option && !option.disabled) option.focus({ preventScroll: true });
      });
      option.addEventListener('click', function (event) {
        event.stopPropagation();
        if (option.disabled) return;
        if (chosen) { close(true); return; }
        close(settings.refocus !== false, true);
        onChoose(choice.value, choice);
      });
      menu.appendChild(option);
      if (chosen && !choice.disabled && !choice.hidden) start = option;
    });
    document.body.appendChild(menu);
    owner.classList.add('is-open');
    owner.setAttribute('aria-expanded', 'true');
    place(menu, owner);
    list = { menu: menu, owner: owner, dispose: null, onClose: settings.onClose };
    start = start || options()[0] || null;
    // The pad runs the list: its own navigation scope, B back to the owner.
    if (window.MeridianInput && typeof window.MeridianInput.attachNavigation === 'function') {
      list.dispose = window.MeridianInput.attachNavigation({ root: menu, initialFocus: start, onBack: function () { close(true); return true; } });
    }
    if (start) {
      start.focus({ preventScroll: true });
      start.scrollIntoView({ block: 'nearest' });
    }
    return menu;
  }

  // A <select> that opens the list: single choice, not a list box, not disabled.
  function usable(select) {
    return !!select && select.tagName === 'SELECT' && !select.multiple && !(select.size > 1) && !select.disabled;
  }
  function openSelect(select) {
    if (!usable(select)) return false;
    select.focus({ preventScroll: true });
    const choices = Array.from(select.options).map(function (option, index) {
      return { value: index, label: option.textContent, disabled: option.disabled, hidden: option.hidden };
    });
    open(select, choices, select.selectedIndex, function (index) {
      if (!document.contains(select) || select.disabled || select.selectedIndex === index) return;
      select.selectedIndex = index;
      select.dispatchEvent(new Event('input', { bubbles: true }));
      select.dispatchEvent(new Event('change', { bubbles: true }));
    });
    return true;
  }

  // Mouse: a <select>'s list opens on press, as a system one would; a press anywhere else closes an open list.
  document.addEventListener('mousedown', function (event) {
    const target = event.target;
    const select = target.closest ? target.closest('select') : null;
    if (list && !list.menu.contains(target) && !list.owner.contains(target) && select !== list.owner) close(false);
    if (!usable(select)) return;
    event.preventDefault();
    if (list && list.owner === select) { close(true); return; }
    openSelect(select);
  }, true);

  // Keyboard, on window so Escape here closes the list and not DovahHub. In the open list: arrows, Page Up/Down and
  // Home/End move, Enter or Space chooses, Escape or Tab closes. On a closed <select>: Enter, Space, Alt+Down or F4
  // opens it, and arrows and typed letters change nothing (the page's own navigation still sees them).
  window.addEventListener('keydown', function (event) {
    if (list) {
      const rows = options();
      const at = rows.indexOf(document.activeElement);
      const step = { ArrowDown: 1, ArrowUp: -1, PageDown: 8, PageUp: -8 }[event.key];
      if (step) { if (rows.length) rows[Math.max(0, Math.min(rows.length - 1, (at < 0 ? 0 : at) + step))].focus(); }
      else if (event.key === 'Home') { if (rows.length) rows[0].focus(); }
      else if (event.key === 'End') { if (rows.length) rows[rows.length - 1].focus(); }
      else if (event.key === 'Enter' || event.key === ' ') { if (at >= 0) rows[at].click(); }
      else if (event.key === 'Escape' || event.key === 'Tab') close(true);
      else return;
      event.preventDefault();
      event.stopImmediatePropagation();
      return;
    }
    const select = document.activeElement;
    if (!usable(select)) return;
    if (event.key === 'Enter' || event.key === ' ' || (event.altKey && event.key === 'ArrowDown') || event.key === 'F4') {
      event.preventDefault();
      event.stopImmediatePropagation();
      openSelect(select);
      return;
    }
    if (/^(Arrow(Up|Down|Left|Right)|Page(Up|Down)|Home|End)$/.test(event.key) || (event.key.length === 1 && !event.ctrlKey && !event.metaKey)) {
      event.preventDefault();
    }
  }, true);

  // The list goes when the page moves under it, or its owner is redrawn away or disabled.
  window.addEventListener('resize', function () { close(false); });
  document.addEventListener('scroll', function (event) { if (list && !list.menu.contains(event.target)) close(false); }, true);
  document.addEventListener('wheel', function (event) { if (list && !list.menu.contains(event.target)) close(false); }, { capture: true, passive: true });
  new MutationObserver(function () { if (list && (!document.contains(list.owner) || list.owner.disabled)) close(false); })
    .observe(document.body, { childList: true, subtree: true, attributes: true, attributeFilter: ['disabled', 'hidden'] });

  // For add-on pages inside DovahHub (and DovahHub's own buttons): the same list, without a native <select>.
  //   DovahHubChoices.open(button, [{ value, label }], currentValue, function (value) { ... }, { onClose })
  //     (version 2: settings.onClose({ chosen }) runs once when the list closes; the owner's is-open and aria-expanded
  //     are cleared before it)
  //   DovahHubChoices.button({ choices, value, label, onChoose }) -> a <button> that opens the list and shows the chosen
  //     label; button.setChoice(value) changes it from outside without calling onChoose.
  // onChoose runs only when a different choice is made; nothing else (the wheel, keys, the stick, the highlight
  // moving, closing the list) ever calls it.
  function choiceButton(settings) {
    const choices = Array.isArray(settings.choices) ? settings.choices : [];
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'hearth-select wf-choice-button' + (settings.className ? ' ' + settings.className : '');
    button.setAttribute('aria-haspopup', 'listbox');
    button.setAttribute('aria-expanded', 'false');
    if (settings.label) setLabel(button, settings.label);
    let value = settings.value;
    const show = function () {
      const choice = choices.find(function (c) { return String(c.value) === String(value); });
      button.dataset.value = value === undefined ? '' : String(value);
      setText(button, choice ? String(choice.label === undefined ? choice.value : choice.label) : (settings.placeholder || ''));
    };
    button.setChoice = function (next) { value = next; show(); };
    button.addEventListener('click', function () {
      if (list && list.owner === button) { close(true); return; }
      open(button, choices, value, function (next, choice) {
        value = next;
        show();
        if (typeof settings.onChoose === 'function') settings.onChoose(next, choice);
      }, { label: settings.label });
    });
    show();
    return button;
  }

  // DHUB-135 (TEAM-RULES, 17 Nov: no native <select> on any page): a <button data-choice> that stands in for a <select>.
  // Its <option>s (and <optgroup>s) live in a hidden span inside it, and it keeps the parts of a select's interface
  // DovahHub's pages use: value, selectedIndex, options, selectedOptions, length, appendChild / append / replaceChildren
  // / insertBefore / removeChild / add for its options, disabled, and an input and change event when a different option
  // is chosen. A click, Enter, Space or the pad's A opens this list; nothing else changes it. Setting value or
  // selectedIndex from script shows it without an event, as a select does.
  //   <button type="button" class="hearth-select" data-choice id="..."><span hidden data-choice-options>
  //     <option value="a">A</option>...</span></button>      (made into one when this file loads)
  //   DovahHubChoices.create()                               (one made from script, in place of createElement('select'))
  const native = {
    appendChild: Element.prototype.appendChild, append: Element.prototype.append, replaceChildren: Element.prototype.replaceChildren,
    insertBefore: Element.prototype.insertBefore, removeChild: Element.prototype.removeChild
  };
  function upgrade(button) {
    if (!button || button.__dovahhubChoice) return button;
    let holder = Array.from(button.children).find(function (child) { return child.hasAttribute('data-choice-options'); });
    if (!holder) {
      holder = document.createElement('span');
      holder.hidden = true;
      holder.setAttribute('data-choice-options', '');
    }
    Array.from(button.childNodes).forEach(function (node) {
      if (node === holder) return;
      if (node.nodeType === 1 && (node.tagName === 'OPTION' || node.tagName === 'OPTGROUP')) native.appendChild.call(holder, node);
      else node.remove();
    });
    const label = document.createElement('span');
    label.className = 'wf-choice-button__label';
    native.replaceChildren.call(button, label, holder);
    button.type = 'button';
    button.setAttribute('data-choice', '');
    button.classList.add('wf-choice-button');
    button.setAttribute('aria-haspopup', 'listbox');
    button.setAttribute('aria-expanded', 'false');

    let chosen = null, none = false;
    const all = function () { return Array.from(holder.querySelectorAll('option')); };
    // The selected option, as a select picks one: the chosen one while it's still there, else the first marked
    // selected, else the first that isn't disabled.
    const current = function () {
      const list = all();
      if (none) return null;
      if (chosen && list.indexOf(chosen) >= 0) return chosen;
      chosen = list.find(function (o) { return o.defaultSelected; }) || list.find(function (o) { return !o.disabled; }) || list[0] || null;
      return chosen;
    };
    const refresh = function () {
      const option = current();
      setText(label, option ? option.textContent : '');
      button.dataset.value = option ? option.value : '';
    };
    const select = function (option) { chosen = option; none = !option; refresh(); };
    Object.defineProperties(button, {
      __dovahhubChoice: { value: true },
      value: {
        configurable: true,
        get: function () { const option = current(); return option ? option.value : ''; },
        set: function (v) { select(all().find(function (o) { return o.value === String(v); }) || null); }
      },
      selectedIndex: {
        configurable: true,
        get: function () { return all().indexOf(current()); },
        set: function (i) { select(all()[Number(i)] || null); }
      },
      options: { configurable: true, get: function () { return all(); } },
      selectedOptions: { configurable: true, get: function () { const option = current(); return option ? [option] : []; } },
      length: { configurable: true, get: function () { return all().length; } },
      appendChild: { configurable: true, value: function (node) { native.appendChild.call(holder, node); refresh(); return node; } },
      add: { configurable: true, value: function (node) { native.appendChild.call(holder, node); refresh(); } },
      append: { configurable: true, value: function () { native.append.apply(holder, arguments); refresh(); } },
      replaceChildren: { configurable: true, value: function () { chosen = null; none = false; native.replaceChildren.apply(holder, arguments); refresh(); } },
      insertBefore: { configurable: true, value: function (node, ref) { native.insertBefore.call(holder, node, ref && ref.parentNode === holder ? ref : null); refresh(); return node; } },
      removeChild: { configurable: true, value: function (node) { native.removeChild.call(node.parentNode === holder || holder.contains(node) ? node.parentNode : holder, node); refresh(); return node; } }
    });
    // Options re-labelled later (a language change, text set after appending) show at once.
    new MutationObserver(refresh).observe(holder, { childList: true, subtree: true, characterData: true, attributes: true, attributeFilter: ['label', 'disabled', 'hidden'] });
    button.addEventListener('click', function () {
      if (button.disabled) return;
      if (list && list.owner === button) { close(true); return; }
      const choices = [];
      Array.from(holder.children).forEach(function (child) {
        const add = function (option) {
          const index = all().indexOf(option);
          choices.push({ value: index, label: option.textContent, disabled: option.disabled, hidden: option.hidden, data: { value: option.value } });
        };
        if (child.tagName === 'OPTGROUP') {
          choices.push({ value: 'group-' + choices.length, label: child.label, disabled: true, data: { group: '1' } });
          Array.from(child.querySelectorAll('option')).forEach(add);
        } else if (child.tagName === 'OPTION') add(child);
      });
      open(button, choices, all().indexOf(current()), function (index) {
        const option = all()[Number(index)];
        if (!option || option === current()) return;
        select(option);
        button.dispatchEvent(new Event('input', { bubbles: true }));
        button.dispatchEvent(new Event('change', { bubbles: true }));
      });
    });
    refresh();
    return button;
  }
  function create() {
    const button = document.createElement('button');
    button.className = 'hearth-select';
    return upgrade(button);
  }
  document.querySelectorAll('button[data-choice]').forEach(upgrade);

  window.DovahHubChoices = Object.freeze({
    version: 3,
    create: create,
    upgrade: upgrade,
    open: function (owner, choices, current, onChoose, settings) {
      if (!owner || !Array.isArray(choices) || typeof onChoose !== 'function') return null;
      return open(owner, choices, current, onChoose, settings);
    },
    close: function () { close(true); },
    isOpen: function () { return !!list; },
    openSelect: openSelect,
    button: choiceButton
  });
  window.dovahhubCloseDropdown = function () { close(false); };
}());
