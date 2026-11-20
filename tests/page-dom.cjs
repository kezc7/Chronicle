// Chronicle — Skyrim SE/AE chat and activity log
// Copyright (C) 2026 kezc7
//
// This file is part of Chronicle.
// Chronicle is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// Chronicle is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details. See LICENSE.

'use strict';

// A small stand-in for the browser's DOM, enough to run Chronicle's own page
// in a test: index.html parsed into elements with ids, classes, attributes and
// parents; simple CSS selectors (tag, .class, #id, [attr], [attr=value],
// descendants, comma lists); events with a capture and a bubble phase; and the
// form controls' values; a window that events reach after the document. No
// layout, and a MutationObserver that never reports. A selector it can't read
// throws, so a test never passes on a query that silently matched nothing.
// Tests only.

const VOID = new Set(['input', 'br', 'img', 'meta', 'link', 'hr', 'source', 'col', 'wbr']);
const RAW = new Set(['script', 'style']);
const camel = name => name.replace(/-([a-z])/g, (_, c) => c.toUpperCase());
const entities = text => text.replace(/&(amp|lt|gt|quot|#39|minus|nbsp);/g, (_, e) =>
  ({amp: '&', lt: '<', gt: '>', quot: '"', '#39': "'", minus: '−', nbsp: ' '})[e]);

function parseAttributes(text) {
  const attributes = {};
  for (const m of text.matchAll(/([^\s="'/>]+)(?:\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s"'>]+)))?/g)) {
    attributes[m[1].toLowerCase()] = entities(m[2] ?? m[3] ?? m[4] ?? '');
  }
  return attributes;
}

// HTML into child elements of `parent`. Text is kept only as an element's own
// textContent; comments, scripts and styles are dropped.
function parseInto(doc, parent, html) {
  html = html.replace(/<!--[\s\S]*?-->/g, '');
  const stack = [parent];
  const tag = /<(\/?)([a-zA-Z][\w-]*)((?:"[^"]*"|'[^']*'|[^'">])*?)(\/?)>/g;
  let last = 0;
  let m;
  while ((m = tag.exec(html))) {
    const text = html.slice(last, m.index);
    if (text.trim() && stack.length > 1) stack[stack.length - 1]._text += entities(text.trim());
    last = tag.lastIndex;
    const name = m[2].toLowerCase();
    if (m[1]) {
      const at = stack.map(e => e.tagName).lastIndexOf(name.toUpperCase());
      if (at > 0) stack.length = at;
      continue;
    }
    const element = doc.createElement(name, parseAttributes(m[3]));
    stack[stack.length - 1].appendChild(element);
    if (RAW.has(name)) {
      const end = html.indexOf('</' + name, last);
      last = tag.lastIndex = end < 0 ? html.length : end;
      continue;
    }
    if (!VOID.has(name) && !m[4]) stack.push(element);
  }
}

function parseSelector(selector) {
  return selector.split(',').map(part => part.trim().split(/\s+/).map(compound => {
    const head = /^([a-zA-Z][\w-]*|\*)?/.exec(compound);
    const result = {tag: head[1] && head[1] !== '*' ? head[1].toUpperCase() : '', classes: [], id: '', attributes: []};
    const token = /\.([\w-]+)|#([\w-]+)|\[([\w-]+)(?:=(?:"([^"]*)"|'([^']*)'|([^\]]*)))?\]/y;
    token.lastIndex = head[0].length;
    while (token.lastIndex < compound.length) {
      const t = token.exec(compound);
      if (!t) throw new Error(`page-dom: unsupported selector "${selector}"`);
      if (t[1]) result.classes.push(t[1]);
      else if (t[2]) result.id = t[2];
      else result.attributes.push({name: t[3].toLowerCase(), value: t[4] ?? t[5] ?? t[6]});
    }
    return result;
  }));
}

function attributeOf(element, name) {
  if (name.startsWith('data-')) {
    const value = element.dataset[camel(name.slice(5))];
    return value === undefined ? null : String(value);
  }
  if (name === 'type') return element.type;
  return Object.prototype.hasOwnProperty.call(element.attributes, name) ? element.attributes[name] : null;
}

function matchesCompound(element, c) {
  if (c.tag && element.tagName !== c.tag) return false;
  if (c.id && element.id !== c.id) return false;
  if (!c.classes.every(name => element.classList.contains(name))) return false;
  return c.attributes.every(a => {
    const value = attributeOf(element, a.name);
    return value !== null && (a.value === undefined || value === a.value);
  });
}

function matches(element, chains) {
  return chains.some(chain => {
    if (!matchesCompound(element, chain[chain.length - 1])) return false;
    let node = element.parentNode;
    for (let i = chain.length - 2; i >= 0; --i) {
      while (node && !(node instanceof Element && matchesCompound(node, chain[i]))) node = node.parentNode;
      if (!node) return false;
      node = node.parentNode;
    }
    return true;
  });
}

class Event {
  constructor(type, init = {}) {
    this.type = type;
    this.bubbles = init.bubbles === true;
    this.target = null;
    this.currentTarget = null;
    this.defaultPrevented = false;
    this.stopped = false;
    Object.assign(this, init.detail ? {detail: init.detail} : {});
  }
  preventDefault() { this.defaultPrevented = true; }
  stopPropagation() { this.stopped = true; }
  stopImmediatePropagation() { this.stopped = true; this.immediate = true; }
}
class KeyboardEvent extends Event {
  constructor(type, init = {}) {
    super(type, init);
    for (const key of ['key', 'code', 'keyCode', 'repeat', 'shiftKey', 'ctrlKey', 'altKey', 'metaKey']) {
      this[key] = init[key] ?? (key === 'key' || key === 'code' ? '' : key === 'keyCode' ? 0 : false);
    }
  }
}
class FocusEvent extends Event {}
class MutationObserver {
  constructor(callback) { this.callback = callback; }
  observe() {}
  disconnect() {}
  takeRecords() { return []; }
}

class Node {
  constructor(doc) {
    this.ownerDocument = doc;
    this.parentNode = null;
    this.children = [];
    this.listeners = [];
  }
  addEventListener(type, listener, options) {
    const capture = options === true || Boolean(options && options.capture === true);
    this.listeners.push({type, listener, capture});
  }
  removeEventListener(type, listener) {
    this.listeners = this.listeners.filter(l => l.type !== type || l.listener !== listener);
  }
  dispatchEvent(event) {
    event.target = this;
    const path = [];
    for (let node = this; node; node = node.parentNode || (node instanceof Document ? node.defaultView : null)) path.push(node);
    const run = (node, capture) => {
      event.currentTarget = node;
      for (const l of [...node.listeners]) {
        if (event.immediate) return;
        if (l.type === event.type && (node === this || l.capture === capture)) l.listener.call(node, event);
      }
    };
    for (let i = path.length - 1; i > 0 && !event.stopped; --i) run(path[i], true);
    if (!event.stopped) run(this, false);
    for (let i = 1; event.bubbles && i < path.length && !event.stopped; ++i) run(path[i], false);
    return !event.defaultPrevented;
  }
  descendants() {
    const out = [];
    const walk = node => node.children.forEach(child => { out.push(child); walk(child); });
    walk(this);
    return out;
  }
  querySelectorAll(selector) {
    const chains = parseSelector(selector);
    return this.descendants().filter(element => matches(element, chains));
  }
  querySelector(selector) { return this.querySelectorAll(selector)[0] || null; }
  getElementById(id) { return this.descendants().find(element => element.id === id) || null; }
  appendChild(child) {
    if (child.parentNode) child.remove();
    child.parentNode = this;
    this.children.push(child);
    return child;
  }
  append(...items) { items.filter(item => item instanceof Node).forEach(item => this.appendChild(item)); }
  prepend(...items) {
    items.filter(item => item instanceof Node).reverse().forEach(item => {
      if (item.parentNode) item.remove();
      item.parentNode = this;
      this.children.unshift(item);
    });
  }
  insertBefore(child, before) {
    if (!before) return this.appendChild(child);
    if (child.parentNode) child.remove();
    child.parentNode = this;
    this.children.splice(this.children.indexOf(before), 0, child);
    return child;
  }
  replaceChildren(...items) {
    this.children.forEach(child => { child.parentNode = null; });
    this.children = [];
    this.append(...items);
  }
  removeChild(child) { child.remove(); return child; }
  contains(node) { for (; node; node = node.parentNode) if (node === this) return true; return false; }
  get childNodes() { return [...this.children]; }
  get firstChild() { return this.children[0] || null; }
  get lastChild() { return this.children[this.children.length - 1] || null; }
  get firstElementChild() { return this.firstChild; }
  get childElementCount() { return this.children.length; }
}

function makeClassList(element) {
  const read = () => (element.attributes.class || '').split(/\s+/).filter(Boolean);
  const write = names => { element.attributes.class = [...new Set(names)].join(' '); };
  return {
    add: (...names) => write([...read(), ...names]),
    remove: (...names) => write(read().filter(name => !names.includes(name))),
    toggle(name, force) {
      const on = force === undefined ? !read().includes(name) : !!force;
      if (on) this.add(name); else this.remove(name);
      return on;
    },
    contains: name => read().includes(name),
    get length() { return read().length; },
    [Symbol.iterator]: () => read()[Symbol.iterator](),
  };
}

class Element extends Node {
  constructor(doc, tag, attributes = {}) {
    super(doc);
    this.nodeType = 1;
    this.tagName = tag.toUpperCase();
    this.attributes = {...attributes};
    this.classList = makeClassList(this);
    this.dataset = {};
    for (const [name, value] of Object.entries(attributes)) {
      if (name.startsWith('data-')) this.dataset[camel(name.slice(5))] = value;
    }
    this.style = {setProperty() {}, removeProperty() {}, getPropertyValue: () => ''};
    this._text = '';
    this._value = attributes.value ?? '';
    this._selected = null;
    this.checked = 'checked' in attributes;
    this.disabled = 'disabled' in attributes;
    this.hidden = 'hidden' in attributes;
    this.readOnly = 'readonly' in attributes;
    this.scrollTop = 0; this.scrollLeft = 0; this.scrollHeight = 0; this.clientHeight = 0; this.clientWidth = 0;
    this.offsetHeight = 0; this.offsetWidth = 0; this.offsetLeft = 0; this.offsetTop = 0;
  }
  get id() { return this.attributes.id || ''; }
  set id(value) { this.attributes.id = String(value); }
  get className() { return this.attributes.class || ''; }
  set className(value) { this.attributes.class = String(value); }
  get type() {
    const type = this.attributes.type;
    if (type) return type.toLowerCase();
    return this.tagName === 'INPUT' ? 'text' : this.tagName === 'BUTTON' ? 'submit' : '';
  }
  set type(value) { this.attributes.type = String(value); }
  get min() { return this.attributes.min ?? ''; }
  set min(value) { this.attributes.min = String(value); }
  get max() { return this.attributes.max ?? ''; }
  set max(value) { this.attributes.max = String(value); }
  get step() { return this.attributes.step ?? ''; }
  set step(value) { this.attributes.step = String(value); }
  get placeholder() { return this.attributes.placeholder ?? ''; }
  set placeholder(value) { this.attributes.placeholder = String(value); }
  get title() { return this.attributes.title ?? ''; }
  set title(value) { this.attributes.title = String(value); }
  get options() { return this.tagName === 'SELECT' ? this.querySelectorAll('option') : []; }
  get defaultSelected() { return 'selected' in this.attributes; }
  get label() { return this.attributes.label ?? this.textContent; }
  get selectedIndex() { return this.options.findIndex(option => option.value === this.value); }
  set selectedIndex(index) { const option = this.options[index]; this._selected = option ? option.value : ''; }
  get value() {
    if (this.tagName === 'SELECT') {
      const options = this.options;
      if (this._selected !== null) return this._selected;
      const chosen = options.find(option => 'selected' in option.attributes) || options[0];
      return chosen ? chosen.value : '';
    }
    if (this.tagName === 'OPTION') return this.attributes.value ?? this.textContent;
    return this._value;
  }
  set value(value) {
    value = value === null || value === undefined ? '' : String(value);
    if (this.tagName === 'SELECT') {
      this._selected = this.options.some(option => option.value === value) ? value : '';
    } else if (this.tagName === 'OPTION') {
      this.attributes.value = value;
    } else {
      this._value = value;
    }
  }
  get textContent() { return this._text + this.children.map(child => child.textContent).join(''); }
  set textContent(value) { this.replaceChildren(); this._text = String(value ?? ''); }
  get innerText() { return this.textContent; }
  set innerText(value) { this.textContent = value; }
  get innerHTML() { return ''; }
  set innerHTML(html) { this.replaceChildren(); this._text = ''; parseInto(this.ownerDocument, this, String(html)); }
  get nextElementSibling() {
    const siblings = this.parentNode ? this.parentNode.children : [];
    return siblings[siblings.indexOf(this) + 1] || null;
  }
  get previousElementSibling() {
    const siblings = this.parentNode ? this.parentNode.children : [];
    return siblings[siblings.indexOf(this) - 1] || null;
  }
  get parentElement() { return this.parentNode instanceof Element ? this.parentNode : null; }
  setAttribute(name, value) {
    name = name.toLowerCase();
    this.attributes[name] = String(value);
    if (name.startsWith('data-')) this.dataset[camel(name.slice(5))] = String(value);
    if (['checked', 'disabled', 'hidden'].includes(name)) this[name] = true;
  }
  getAttribute(name) { return attributeOf(this, name.toLowerCase()); }
  hasAttribute(name) { return this.getAttribute(name) !== null; }
  removeAttribute(name) {
    name = name.toLowerCase();
    delete this.attributes[name];
    if (name.startsWith('data-')) delete this.dataset[camel(name.slice(5))];
    if (['checked', 'disabled', 'hidden'].includes(name)) this[name] = false;
  }
  toggleAttribute(name, force) {
    const on = force === undefined ? !this.hasAttribute(name) : !!force;
    if (on) this.setAttribute(name, ''); else this.removeAttribute(name);
    return on;
  }
  matches(selector) { return matches(this, parseSelector(selector)); }
  closest(selector) {
    const chains = parseSelector(selector);
    for (let node = this; node instanceof Element; node = node.parentNode) if (matches(node, chains)) return node;
    return null;
  }
  remove() {
    if (!this.parentNode) return;
    const siblings = this.parentNode.children;
    siblings.splice(siblings.indexOf(this), 1);
    this.parentNode = null;
  }
  // Ticks a checkbox before the click reaches listeners, then reports the
  // change, as a browser does.
  click() {
    if (this.disabled) return;
    const box = this.tagName === 'INPUT' && (this.type === 'checkbox' || this.type === 'radio');
    if (box) this.checked = this.type === 'radio' ? true : !this.checked;
    this.dispatchEvent(new Event('click', {bubbles: true}));
    if (box) {
      this.dispatchEvent(new Event('input', {bubbles: true}));
      this.dispatchEvent(new Event('change', {bubbles: true}));
    }
  }
  focus() {
    const doc = this.ownerDocument;
    if (doc.activeElement === this) return;
    if (doc.activeElement && doc.activeElement !== doc.body) doc.activeElement.blur();
    doc.activeElement = this;
    this.dispatchEvent(new Event('focus'));
  }
  blur() {
    const doc = this.ownerDocument;
    if (doc.activeElement !== this) return;
    doc.activeElement = doc.body;
    this.dispatchEvent(new Event('blur'));
  }
  select() {}
  scrollIntoView() {}
  scrollTo() {}
  getBoundingClientRect() { return {left: 0, top: 0, right: 0, bottom: 0, width: 0, height: 0, x: 0, y: 0}; }
  getClientRects() { return []; }
}

// Events that bubble past the document reach it, as in a browser.
class Window extends Node {
  constructor() {
    super(null);
    this.innerWidth = 1920;
    this.innerHeight = 1080;
    this.devicePixelRatio = 1;
    this.location = {search: '', href: ''};
  }
}

class Document extends Node {
  constructor(html, defaultView = null) {
    super(null);
    this.defaultView = defaultView;
    this.ownerDocument = this;
    parseInto(this, this, html);
    this.documentElement = this.children.find(child => child.tagName === 'HTML') || this.appendChild(new Element(this, 'html'));
    this.head = this.documentElement.querySelector('head') || this.documentElement.appendChild(new Element(this, 'head'));
    this.body = this.documentElement.querySelector('body') || this.documentElement.appendChild(new Element(this, 'body'));
    this.activeElement = this.body;
  }
  createElement(tag, attributes) { return new Element(this, tag, attributes); }
  createTextNode(text) { const node = new Element(this, '#text'); node._text = String(text); return node; }
  createDocumentFragment() { return new Element(this, '#fragment'); }
}

module.exports = {Document, Element, Event, KeyboardEvent, FocusEvent, MutationObserver, Window};
