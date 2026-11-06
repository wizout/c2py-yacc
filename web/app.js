// Веб-інтерфейс: редактор C, AST на d3.js, токени, згенерований Python, вивід.
// Сервер (server.py) запускає build/c2py і повертає все в одному JSON.

const CATEGORY = {
  Program: 'decl', FuncDef: 'decl', FuncProto: 'decl', Params: 'decl', Param: 'decl',
  VarDecl: 'decl', Var: 'decl', InitList: 'decl',
  Block: 'stmt', If: 'stmt', While: 'stmt', DoWhile: 'stmt', For: 'stmt', Return: 'stmt',
  Break: 'stmt', Continue: 'stmt', Empty: 'stmt', ExprStmt: 'stmt',
  Assign: 'op', BinOp: 'op', UnOp: 'op', PreInc: 'op', PostInc: 'op', Ternary: 'op',
  Call: 'op', Index: 'op', Cast: 'op',
  Id: 'id', Int: 'lit', Double: 'lit', Char: 'lit', String: 'lit',
};
const EXPR_KINDS = new Set(['Assign', 'BinOp', 'UnOp', 'PreInc', 'PostInc', 'Ternary', 'Call', 'Index', 'Cast']);

const $ = (s) => document.querySelector(s);
const cssVar = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();
const color = (kind) => cssVar('--' + (CATEGORY[kind] || 'stmt'));

// ---------- редактори ----------
const editor = CodeMirror.fromTextArea($('#code'), {
  mode: 'text/x-csrc', lineNumbers: true, indentUnit: 4, tabSize: 4,
  extraKeys: { 'Ctrl-Enter': run, 'Cmd-Enter': run },
});
const pyView = CodeMirror.fromTextArea($('#py'), { mode: 'python', lineNumbers: true, readOnly: true });

// ---------- вкладки ----------
document.querySelectorAll('.tabs button').forEach((b) => b.addEventListener('click', () => showTab(b.dataset.tab)));
function showTab(name) {
  document.querySelectorAll('.tabs button').forEach((b) => b.classList.toggle('active', b.dataset.tab === name));
  document.querySelectorAll('.pane').forEach((p) => p.classList.toggle('active', p.id === 'pane-' + name));
  if (name === 'python') pyView.refresh();
  if (name === 'ast' && root) fit(false);
}

// ---------- приклади ----------
let examples = [];
fetch('/api/examples').then((r) => r.json()).then((list) => {
  examples = list;
  const sel = $('#example');
  sel.innerHTML = list.map((e, i) => `<option value="${i}">${e.name}</option>`).join('');
  const start = Math.max(0, list.findIndex((e) => e.name === 'misc.c'));
  sel.value = start;
  editor.setValue(list[start].code);
  run();
});
$('#example').addEventListener('change', (e) => { editor.setValue(examples[e.target.value].code); run(); });
$('#run').addEventListener('click', run);

// ---------- компіляція ----------
let errLines = [];
async function run() {
  const status = $('#status');
  status.className = 'status';
  status.textContent = 'Розбираю…';
  let res;
  try {
    res = await fetch('/api/compile', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ code: editor.getValue() }),
    }).then((r) => r.json());
  } catch (e) {
    status.className = 'status bad';
    status.textContent = 'Сервер не відповідає — запустіть python3 server.py';
    return;
  }
  showDiagnostics(res.errors);
  showTokens(res.tokens);
  pyView.setValue(res.python || '# код не згенеровано через помилки');
  showOutput(res);
  if (res.ast) drawTree(res.ast);

  const n = res.errors.length;
  status.className = 'status ' + (n ? 'bad' : 'ok');
  status.textContent = n ? `Помилок: ${n}` : 'Без помилок';
}

function showDiagnostics(errors) {
  errLines.forEach((l) => editor.removeLineClass(l, 'background', 'line-err'));
  errLines = [];
  const box = $('#diag');
  box.innerHTML = '';
  errors.forEach((msg) => {
    const p = document.createElement('p');
    p.textContent = msg;
    const m = msg.match(/line (\d+)/);
    if (m) {
      const ln = +m[1] - 1;
      editor.addLineClass(ln, 'background', 'line-err');
      errLines.push(ln);
      p.addEventListener('click', () => { editor.setCursor(ln, 0); editor.focus(); });
    }
    box.appendChild(p);
  });
}

function showTokens(tokens) {
  $('#tok-body').innerHTML = (tokens || []).map((t) =>
    `<tr><td>${t.line}</td><td>${esc(t.token)}</td><td>${esc(t.text)}</td></tr>`).join('');
}

function showOutput(res) {
  const fmt = (out, err) => esc(out) + (err ? `<span class="err">${esc(err)}</span>` : '');
  $('#run-out').innerHTML = res.run ? fmt(res.run.stdout, res.run.stderr) : '';
  $('#py-out').innerHTML = res.py ? fmt(res.py.stdout, res.py.stderr) : '';
  $('#run-code').textContent = res.run ? `код ${res.run.code}` : '';
  $('#py-code').textContent = res.py ? `код ${res.py.code}` : '';
  const m = $('#match');
  if (res.run && res.py) {
    const same = res.run.stdout === res.py.stdout && res.run.code === res.py.code;
    m.className = 'match ' + (same ? 'ok' : 'bad');
    m.textContent = same ? '✓ Виводи збігаються' : '✗ Виводи відрізняються';
  } else { m.textContent = ''; }
}

function esc(s) {
  return String(s ?? '').replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
}

// ---------- дерево (d3) ----------
const svg = d3.select('#tree-svg');
const g = svg.append('g');
const gLinks = g.append('g');
const gNodes = g.append('g');
const zoom = d3.zoom().scaleExtent([0.1, 3]).on('zoom', (e) => g.attr('transform', e.transform));
svg.call(zoom).on('dblclick.zoom', null);

const measure = document.createElement('canvas').getContext('2d');
function textWidth(s, font) { measure.font = font; return measure.measureText(s).width; }

let root = null;
let uid = 0;
const DY = 66;

function drawTree(data) {
  root = d3.hierarchy(data);
  root.each((d) => {
    d.id = ++uid;
    const lw = textWidth(d.data.label, '600 13px JetBrains Mono');
    const kw = textWidth(d.data.kind, '10px IBM Plex Sans');
    // запас на випадок, якщо веб-шрифт не завантажився і моноширинний fallback ширший
    d.w = Math.max(lw, d.data.label.length * 8.2, kw) + 18;
  });
  // великі дерева спочатку показуємо з згорнутими виразами — інакше нічого не видно
  if (root.descendants().length > 120) collapseExprs();
  update(root, false);
  fit(false);
}

function collapseExprs() {
  eachAll(root, (d) => {
    if (EXPR_KINDS.has(d.data.kind) && d.children && !(d.parent && EXPR_KINDS.has(d.parent.data.kind))) {
      d._children = d.children; d.children = null;
    }
  });
}

function layout() {
  d3.tree().nodeSize([1, DY]).separation((a, b) => (a.w + b.w) / 2 + (a.parent === b.parent ? 10 : 22))(root);
}

function update(source, animate = true) {
  layout();
  const nodes = root.descendants();
  const links = root.links();
  const t = svg.transition().duration(animate ? 300 : 0);
  const x0 = source.x0 ?? source.x, y0 = source.y0 ?? source.y;

  const node = gNodes.selectAll('g.node').data(nodes, (d) => d.id);
  const enter = node.enter().append('g').attr('class', 'node')
    .attr('transform', `translate(${x0},${y0})`)
    .on('click', (e, d) => toggle(d))
    .on('mouseenter', (e, d) => hover(d, true, e))
    .on('mousemove', (e) => moveTip(e))
    .on('mouseleave', (e, d) => hover(d, false));
  enter.append('rect').attr('rx', 6).attr('y', -16).attr('height', 32);
  enter.append('text').attr('class', 'kind').attr('text-anchor', 'middle').attr('y', -4);
  enter.append('text').attr('class', 'label').attr('text-anchor', 'middle').attr('y', 11);

  const all = enter.merge(node);
  all.classed('collapsed', (d) => !!d._children);
  all.select('rect').attr('x', (d) => -d.w / 2).attr('width', (d) => d.w).attr('stroke', (d) => color(d.data.kind));
  all.select('.kind').text((d) => d.data.kind);
  all.select('.label').text((d) => d.data.label).attr('fill', (d) => color(d.data.kind));
  all.transition(t).attr('transform', (d) => `translate(${d.x},${d.y})`).style('opacity', 1);
  node.exit().transition(t).attr('transform', `translate(${source.x},${source.y})`).style('opacity', 0).remove();

  const path = (s, d) => `M${s.x},${s.y + 16} C${s.x},${(s.y + d.y) / 2} ${d.x},${(s.y + d.y) / 2} ${d.x},${d.y - 16}`;
  const link = gLinks.selectAll('path.link').data(links, (d) => d.target.id);
  link.enter().append('path').attr('class', 'link')
    .attr('d', () => path({ x: x0, y: y0 }, { x: x0, y: y0 + 32 }))
    .merge(link).transition(t).attr('d', (d) => path(d.source, d.target)).style('opacity', 1);
  link.exit().transition(t).attr('d', () => path(source, { x: source.x, y: source.y + 32 })).remove();

  root.each((d) => { d.x0 = d.x; d.y0 = d.y; });
}

function toggle(d) {
  if (d.children) { d._children = d.children; d.children = null; }
  else if (d._children) { d.children = d._children; d._children = null; }
  else return;
  update(d);
}

function eachAll(d, fn) { fn(d); (d.children || d._children || []).forEach((c) => eachAll(c, fn)); }

$('#expand').addEventListener('click', () => {
  if (!root) return;
  eachAll(root, (d) => { if (d._children) { d.children = d._children; d._children = null; } });
  update(root); setTimeout(() => fit(true), 320);
});
$('#collapse').addEventListener('click', () => {
  if (!root) return;
  collapseExprs();
  update(root); setTimeout(() => fit(true), 320);
});
$('#fit').addEventListener('click', () => fit(true, true));

function fit(animate, whole = false) {
  if (!root) return;
  const box = $('#tree').getBoundingClientRect();
  if (!box.width) return;
  const nodes = root.descendants();
  const minX = d3.min(nodes, (d) => d.x - d.w / 2), maxX = d3.max(nodes, (d) => d.x + d.w / 2);
  const maxY = d3.max(nodes, (d) => d.y) + 20;
  const fitK = Math.min(1.2, (box.width - 40) / (maxX - minX), (box.height - 40) / (maxY + 20));
  // при першому показі дрібніше 0.75 не робимо — текст має читатись, решту можна прокрутити
  const minK = whole ? 0.05 : 0.75;
  const k = Math.max(fitK, minK);
  // якщо дерево не влазить, тримаємо в кадрі корінь, а не центр
  const tx = fitK >= minK ? box.width / 2 - k * (minX + maxX) / 2
                          : Math.min(20 - k * minX, box.width / 2 - k * root.x);
  const tr = d3.zoomIdentity.translate(tx, 30).scale(k);
  (animate ? svg.transition().duration(400) : svg).call(zoom.transform, tr);
}

// підсвітка: вузол <-> рядок коду
let hlLine = null;
function hover(d, on, e) {
  const tip = $('#tip');
  if (hlLine !== null) { editor.removeLineClass(hlLine, 'background', 'line-hl'); hlLine = null; }
  if (!on) { tip.style.opacity = 0; return; }
  hlLine = d.data.line - 1;
  editor.addLineClass(hlLine, 'background', 'line-hl');
  editor.scrollIntoView({ line: hlLine, ch: 0 }, 80);
  tip.innerHTML = `${esc(d.data.kind)} ${esc(d.data.label)}<br>рядок ${d.data.line}` +
    (d.data.type ? `, тип ${esc(d.data.type)}` : '') +
    (d._children ? `<br>згорнуто: ${d._children.length} дит.` : '');
  tip.style.opacity = 1;
  moveTip(e);
}
function moveTip(e) {
  const box = $('#tree').getBoundingClientRect();
  const tip = $('#tip');
  tip.style.left = Math.min(e.clientX - box.left + 14, box.width - tip.offsetWidth - 6) + 'px';
  tip.style.top = (e.clientY - box.top + 14) + 'px';
}

editor.on('cursorActivity', () => {
  const line = editor.getCursor().line + 1;
  gNodes.selectAll('g.node').classed('hl', (d) => d.data.line === line && d.data.kind !== 'Program');
});

// "Відтворити згортки": LR-парсер будує дерево знизу вгору —
// вузол з'являється в момент згортки (reduce) його правила, тобто в порядку post-order.
$('#replay').addEventListener('click', () => {
  if (!root) return;
  const order = [];
  (function post(d) { (d.children || []).forEach(post); order.push(d); })(root);
  const shown = new Set();
  const nodeSel = gNodes.selectAll('g.node');
  const linkSel = gLinks.selectAll('path.link');
  nodeSel.interrupt().style('opacity', 0.06);
  linkSel.interrupt().style('opacity', 0);
  const step = Math.max(25, Math.min(160, 5000 / order.length));
  const btn = $('#replay');
  btn.disabled = true;
  order.forEach((d, i) => setTimeout(() => {
    shown.add(d.id);
    nodeSel.filter((n) => n.id === d.id).transition().duration(150).style('opacity', 1);
    linkSel.filter((l) => l.source.id === d.id).transition().duration(150).style('opacity', 1);
    btn.textContent = `Згортка ${i + 1}/${order.length}: ${d.data.kind}`;
    if (i === order.length - 1) setTimeout(() => { btn.textContent = 'Відтворити згортки'; btn.disabled = false; }, 600);
  }, i * step));
});

// експорт дерева в SVG-файл
$('#svg').addEventListener('click', () => {
  const clone = svg.node().cloneNode(true);
  clone.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
  const style = document.createElement('style');
  style.textContent = `.link{fill:none;stroke:#c4ccd6;stroke-width:1.5px} rect{fill:#fff;stroke-width:1.5px}
    .kind{font:10px sans-serif;fill:#5d6b82} .label{font:600 13px monospace}`;
  clone.insertBefore(style, clone.firstChild);
  const blob = new Blob([clone.outerHTML], { type: 'image/svg+xml' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = 'ast.svg';
  a.click();
});

window.addEventListener('resize', () => fit(false));
