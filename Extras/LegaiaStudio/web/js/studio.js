// studio.js — LegaiaStudio
//
// Rotas do studio.py:
//   GET  /api/status                -> tabelas, builds, enums, achados
//   GET  /api/table/<id>            -> columns, rows, schema, meta, refs
//   POST /api/table/<id>/save       -> {rows: [[...]]}
//   POST /api/build/<key>           -> inicia
//   GET  /api/build/<key>/log?from= -> log incremental
//   GET  /api/waves                 -> arquivos de ondas (pasta do projeto Unreal)
//   GET  /api/waves/<arquivo>       -> o JSON
//   POST /api/waves/<arquivo>/save  -> grava (backup em Saved/ do projeto)
//   POST /api/waves/<arquivo>/new   -> cria a partir de um modelo
//
// A edição acontece em memória (state.tables[id].rows) e só vai para o disco
// no botão Salvar. Enquanto houver pendência, a sidebar marca a tabela com •.

(function () {
    'use strict';

    const state = {
        status: null,
        tables: {},        // id -> {columns, rows, schema, meta, refs, dirty, sel}
        current: null,     // id da view aberta
        buildTimer: null,
        waves: { listing: null, current: null },   // aba Ondas: {file, data, dirty}
    };

    // --- utilidades ---

    const $ = (sel) => document.querySelector(sel);

    function el(tag, cls, text) {
        const e = document.createElement(tag);
        if (cls) e.className = cls;
        if (text !== undefined && text !== null) e.textContent = String(text);
        return e;
    }

    async function api(path, options) {
        const res = await fetch('/api/' + path, options);
        let payload = null;
        try { payload = await res.json(); } catch (e) { /* resposta sem corpo JSON */ }
        if (!res.ok) throw new Error((payload && payload.error) || res.statusText);
        return payload;
    }

    const post = (path, body) => api(path, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body || {}),
    });

    let toastTimer = null;
    function toast(msg, bad) {
        const t = $('#toast');
        t.textContent = msg;
        t.className = 'show' + (bad ? ' bad' : '');
        clearTimeout(toastTimer);
        toastTimer = setTimeout(() => { t.className = ''; }, 3200);
    }

    function banner(msg) {
        const b = $('#banner');
        b.textContent = msg;
        b.style.display = msg ? 'block' : 'none';
    }

    function confirmDialog(title, text, okLabel, danger) {
        return new Promise((resolve) => {
            $('#modal-title').textContent = title;
            $('#modal-text').textContent = text;
            $('#modal-yes').textContent = okLabel || 'Confirmar';
            $('#modal-yes').className = 'btn ' + (danger === false ? 'primary' : 'danger');
            $('#modal').classList.add('open');

            const close = (v) => {
                $('#modal').classList.remove('open');
                $('#modal-yes').onclick = null;
                $('#modal-no').onclick = null;
                resolve(v);
            };
            $('#modal-yes').onclick = () => close(true);
            $('#modal-no').onclick = () => close(false);
        });
    }

    // Ícones da sidebar (inline: sem sprite, sem <use>, nada para falhar em silêncio)
    const ICONS = {
        item:   'M9 2h6v3l-1 1v3l4 8a3 3 0 0 1-3 4H9a3 3 0 0 1-3-4l4-8V6L9 5z',
        shop:   'M3 9l1.5-5h15L21 9M3 9h18M3 9v10a1 1 0 0 0 1 1h16a1 1 0 0 0 1-1V9M9 20v-6h6v6',
        camera: 'M3 7h4l2-3h6l2 3h4v13H3zM12 16a4 4 0 1 0 0-8 4 4 0 0 0 0 8z',
        grid:   'M4 4h7v7H4zM13 4h7v7h-7zM4 13h7v7H4zM13 13h7v7h-7z',
        check:  'M4 12l5 5L20 6',
        build:  'M14 6l4 4-8 8H6v-4zM3 21h18',
        table:  'M3 5h18v14H3zM3 10h18M9 10v9',
        party:  'M8 11a3.5 3.5 0 1 0 0-7 3.5 3.5 0 0 0 0 7zM2 20a6 6 0 0 1 12 0M17 11a3 3 0 1 0 0-6M16 20a6 6 0 0 1 6-6',
        curve:  'M3 20h18M4 17c4 0 5-11 9-11s5 8 8 8',
        shield: 'M12 3l8 3v6c0 5-3.5 9-8 10-4.5-1-8-5-8-10V6z',
        growth: 'M4 20V13M9 20V9M14 20v-6M19 20V4',
        enemy:  'M12 3a8 8 0 0 0-8 8c0 3 1.5 5 3 6v3h10v-3c1.5-1 3-3 3-6a8 8 0 0 0-8-8zM9 11.5h.01M15 11.5h.01M10 20v-2M14 20v-2',
        waves:  'M14.5 17.5L3 6V3h3l11.5 11.5M13 19l6-6M16 16l4 4M19 21l2-2',
        folder: 'M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z',
    };

    function icon(name) {
        const path = ICONS[name] || ICONS.table;
        return '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7"'
             + ' stroke-linecap="round" stroke-linejoin="round"><path d="' + path + '"/></svg>';
    }

    // ====================================================================
    // SIDEBAR
    // ====================================================================

    function buildSidebar() {
        const tablesNav = $('#nav-tables');
        tablesNav.innerHTML = '';

        state.status.tables.forEach((t) => {
            const b = el('button', 'nav-item');
            b.dataset.go = t.id;
            b.innerHTML = '<span class="nav-ico">' + icon(t.icon) + '</span>'
                        + '<span class="nav-label"></span>'
                        + '<span class="nav-count"></span>';
            b.querySelector('.nav-label').textContent = t.label;
            b.querySelector('.nav-count').textContent = t.rowCount;
            b.disabled = !t.hasCsv;
            b.addEventListener('click', () => go(t.id));
            tablesNav.appendChild(b);
        });

        const buildsNav = $('#nav-builds');
        buildsNav.innerHTML = '';
        state.status.builds.forEach((b) => {
            const btn = el('button', 'nav-item');
            btn.title = b.note;
            btn.innerHTML = '<span class="nav-ico">' + icon('build') + '</span>'
                          + '<span class="nav-label"></span>';
            btn.querySelector('.nav-label').textContent = b.label;
            btn.addEventListener('click', () => runBuild(b));
            buildsNav.appendChild(btn);
        });

        // Ícones fixos do index.html (data-ico)
        document.querySelectorAll('.nav-ico[data-ico]').forEach((s) => {
            if (!s.innerHTML) s.innerHTML = icon(s.dataset.ico);
        });

        document.querySelectorAll('.nav-item[data-go]').forEach((b) => {
            if (!b.dataset.bound) {
                b.dataset.bound = '1';
                b.addEventListener('click', () => go(b.dataset.go));
            }
        });

        buildPaths();
        refreshFindingsBadge();
    }

    /** Rodapé da sidebar: caminho + botão que abre a pasta no explorador. */
    function buildPaths() {
        const box = $('#side-paths');
        box.innerHTML = '';

        const row = (label, path, target, enabled) => {
            const line = el('div', 'pathrow');

            const open = el('button', 'pathbtn');
            open.innerHTML = icon('folder');
            open.title = enabled ? 'Abrir no explorador' : 'Pasta não encontrada';
            open.disabled = !enabled;
            open.addEventListener('click', async () => {
                try {
                    await post('reveal', { target: target });
                } catch (e) {
                    toast('Não consegui abrir: ' + e.message, true);
                }
            });
            line.appendChild(open);

            const txt = el('div', 'pathtxt');
            txt.appendChild(el('b', null, label));
            txt.appendChild(document.createTextNode(' ' + path));
            line.appendChild(txt);
            return line;
        };

        box.appendChild(row('plugin', state.status.pluginDir, 'plugin', true));
        box.appendChild(row('unreal',
            state.status.unrealOk ? state.status.unrealProject
                                  : (state.status.unrealProject || '—') + ' (não encontrado)',
            'unreal', state.status.unrealOk));
    }

    function refreshFindingsBadge() {
        const n = state.status.findings.length;
        const badge = $('#nav-findings');
        badge.textContent = n || '';
        badge.className = 'nav-badge' + (n ? ' has' : '');
    }

    function markNav() {
        document.querySelectorAll('.nav-item[data-go]').forEach((b) => {
            b.classList.toggle('active', b.dataset.go === state.current);
        });
        // Marca de alterações não salvas por tabela
        Object.keys(state.tables).forEach((id) => {
            const btn = document.querySelector('.nav-item[data-go="' + id + '"]');
            if (!btn) return;
            let dot = btn.querySelector('.dirty');
            if (state.tables[id].dirty) {
                if (!dot) {
                    dot = el('span', 'dirty', '•');
                    btn.insertBefore(dot, btn.querySelector('.nav-count'));
                }
            } else if (dot) {
                dot.remove();
            }
            const count = btn.querySelector('.nav-count');
            if (count) count.textContent = state.tables[id].rows.length;
        });
    }

    // ====================================================================
    // NAVEGAÇÃO
    // ====================================================================

    async function go(id) {
        state.current = id;
        markNav();

        document.querySelectorAll('.view').forEach((v) => v.classList.remove('active'));
        let view = document.querySelector('.view[data-view="' + id + '"]');
        if (!view) {
            view = el('section', 'view');
            view.dataset.view = id;
            $('#views').appendChild(view);
            if (id === 'overview') renderOverview(view);
            else if (id === 'validation') renderValidation(view);
            else if (id === 'waves') await openWaves(view);
            else await openTable(id, view);
        } else if (id === 'validation') {
            renderValidation(view);
        }
        view.classList.add('active');
    }

    // ====================================================================
    // VISÃO GERAL
    // ====================================================================

    function renderOverview(view) {
        view.innerHTML = '';
        const page = el('div', 'page');

        const s1 = el('section');
        s1.appendChild(el('h2', null, 'DataTables do plugin'));
        const grid = el('div', 'grid');
        state.status.tables.forEach((t) => {
            const card = el('div', 'card');
            const head = el('div', 'tcard-head');
            head.appendChild(el('span', 'tcard-name', t.id));
            head.appendChild(el('span', 'tcard-rows', t.hasCsv ? t.rowCount + ' linhas' : '—'));
            card.appendChild(head);
            card.appendChild(el('div', 'tcard-struct', t.struct + (t.header ? '  ·  ' + t.header : '')));

            const checks = el('div', 'checks');
            checks.appendChild(check(t.hasStruct, 'struct C++' + (t.hasStruct ? ' (' + t.fieldCount + ' campos)' : ' ausente')));
            checks.appendChild(check(t.hasCsv, t.hasCsv ? 'CSV com ' + t.columnCount + ' colunas' : 'CSV ausente'));
            checks.appendChild(!state.status.unrealOk
                ? check(null, 'DataTable — projeto Unreal não configurado')
                : check(t.hasAsset, t.hasAsset ? 'importada: ' + t.assetPath
                                               : 'sem ' + t.id + '.uasset no projeto'));
            card.appendChild(checks);
            grid.appendChild(card);
        });
        s1.appendChild(grid);
        page.appendChild(s1);

        const s2 = el('section');
        s2.appendChild(el('h2', null, 'Ainda sem DataTable'));
        const p = el('p', 'muted');
        p.innerHTML = 'Tabelas que existem no LegaiaRE mas ainda não viraram DataTable. '
                    + 'O caminho é: copiar o <code>.toml</code> para <code>Docs/Data/gamedata/</code>, '
                    + 'gerar o CSV e pedir a implementação do struct C++.';
        s2.appendChild(p);
        const pend = el('div', 'pend');
        state.status.pending.forEach((x) => {
            pend.appendChild(el('span', x.inPlugin ? 'here' : '',
                x.name + (x.inPlugin ? '  · toml no plugin' : '  · só no LegaiaRE')));
        });
        s2.appendChild(pend);
        page.appendChild(s2);

        view.appendChild(page);
    }

    function check(value, label) {
        const row = el('div', 'check');
        row.appendChild(el('span', 'dot ' + (value === null ? 'warn' : value ? 'ok' : 'no')));
        row.appendChild(el('span', null, label));
        return row;
    }

    // ====================================================================
    // VALIDAÇÃO
    // ====================================================================

    function renderValidation(view) {
        view.innerHTML = '';
        const page = el('div', 'page');
        page.appendChild(el('h2', null, 'Validações cruzadas'));

        const p = el('p', 'muted');
        p.innerHTML = 'Erros que só apareceriam como warning no log do Unreal em runtime: '
                    + 'item de loja inexistente, <code>FeaturedItems</code> fora do '
                    + '<code>Inventory</code>, RowName repetido e coluna do CSV que não bate com o struct C++.';
        page.appendChild(p);

        const box = el('div', 'card');
        const findings = state.status.findings;
        if (!findings.length) {
            const ok = el('div', 'all-good');
            ok.appendChild(el('span', 'dot ok'));
            ok.appendChild(document.createTextNode('Nenhum problema encontrado.'));
            box.appendChild(ok);
        } else {
            findings.slice().sort((a, b) => (a.level === b.level ? 0 : a.level === 'error' ? -1 : 1))
                .forEach((f) => {
                    const row = el('div', 'finding ' + f.level);
                    row.appendChild(el('span', 'lvl', f.level === 'error' ? 'erro' : 'aviso'));
                    row.appendChild(el('span', 'where', f.table + ' · ' + f.row));
                    row.appendChild(el('span', 'msg', f.field + ': ' + f.message));
                    box.appendChild(row);
                });
        }
        page.appendChild(box);
        view.appendChild(page);
    }

    // ====================================================================
    // TABELA: lista + ficha
    // ====================================================================

    async function openTable(id, view) {
        view.innerHTML = '';
        view.appendChild(el('div', 'page').appendChild(el('p', 'muted', 'Carregando ' + id + '…')).parentNode);

        let data;
        try {
            data = await api('table/' + id);
        } catch (e) {
            view.innerHTML = '';
            banner('Não consegui abrir ' + id + ': ' + e.message);
            return;
        }

        data.dirty = false;
        data.sel = data.rows.length ? 0 : -1;
        state.tables[id] = data;

        view.innerHTML = '';

        // --- coluna do meio ---
        const list = el('div', 'reclist');
        const top = el('div', 'reclist-top');
        const search = el('input');
        search.type = 'search';
        search.placeholder = 'Buscar…';
        top.appendChild(search);

        const info = el('div', 'reclist-info');
        const count = el('span');
        info.appendChild(count);
        const addBtn = el('button', 'btn', '+ Novo');
        addBtn.style.padding = '5px 11px';
        addBtn.style.fontSize = '10px';
        info.appendChild(addBtn);
        top.appendChild(info);
        list.appendChild(top);

        const items = el('div', 'reclist-items');
        list.appendChild(items);
        view.appendChild(list);

        // --- coluna da direita ---
        const form = el('div', 'form');
        view.appendChild(form);

        const t = state.tables[id];

        function renderList() {
            const q = search.value.trim().toLowerCase();
            items.innerHTML = '';
            let shown = 0;

            t.rows.forEach((row, i) => {
                const title = displayTitle(t, row);
                const sub = displaySub(t, row);
                const hay = (row.join(' ')).toLowerCase();
                if (q && hay.indexOf(q) === -1) return;
                shown++;

                const b = el('button', 'rec' + (i === t.sel ? ' active' : ''));
                const main = el('div', 'rec-main');
                main.appendChild(el('div', 'rec-title', title));
                main.appendChild(el('div', 'rec-sub', sub));
                b.appendChild(main);

                const badge = displayBadge(t, row);
                if (badge) b.appendChild(el('span', 'rec-badge', badge));

                b.addEventListener('click', () => {
                    t.sel = i;
                    renderList();
                    renderForm();
                });
                items.appendChild(b);
            });

            count.textContent = shown + ' de ' + t.rows.length;
            markNav();
        }

        function renderForm() {
            form.innerHTML = '';
            if (t.sel < 0 || t.sel >= t.rows.length) {
                const empty = el('p', 'muted', 'Nenhum registro selecionado.');
                empty.style.marginTop = '30px';
                form.appendChild(empty);
                return;
            }
            buildForm(t, form, renderList);
        }

        search.addEventListener('input', renderList);

        addBtn.addEventListener('click', async () => {
            const name = prompt('RowName do novo registro (a key usada pelo jogo):', '');
            if (name === null) return;
            const key = name.trim();
            if (!key) return toast('RowName não pode ser vazio', true);
            if (t.rows.some((r) => r[0].trim() === key)) return toast('Já existe: ' + key, true);

            const row = new Array(t.columns.length).fill('');
            row[0] = key;
            // Muitas tabelas repetem a key numa coluna própria (FItemData.Key)
            const iKey = t.columns.indexOf('Key');
            if (iKey > 0) row[iKey] = key;
            t.rows.push(row);
            t.sel = t.rows.length - 1;
            t.dirty = true;
            search.value = '';
            renderList();
            renderForm();
            toast('Criado — lembre de salvar');
        });

        renderList();
        renderForm();
    }

    // --- textos da lista, tirados do meta que o servidor mandou ---

    function colValue(t, row, colName) {
        if (!colName) return '';
        const i = t.columns.indexOf(colName);
        return i >= 0 && i < row.length ? row[i] : '';
    }

    function displayTitle(t, row) {
        return colValue(t, row, t.meta.title) || row[0] || '(sem nome)';
    }

    function displaySub(t, row) {
        const sub = colValue(t, row, t.meta.subtitle);
        return sub ? row[0] + '  ·  ' + sub : row[0];
    }

    function displayBadge(t, row) {
        if (!t.meta.badge) return '';
        const v = colValue(t, row, t.meta.badge);
        return v ? v + (t.meta.badgeSuffix || '') : '';
    }

    // --- a ficha ---

    function buildForm(t, form, renderList) {
        const row = t.rows[t.sel];
        const fields = (t.schema && t.schema.fields) || [];

        const head = el('div', 'form-head');
        const left = el('div');
        left.appendChild(el('div', 'form-title', displayTitle(t, row)));
        left.appendChild(el('div', 'form-key', t.id + '  ·  RowName: ' + row[0]));
        head.appendChild(left);

        const actions = el('div', 'form-actions');
        const save = el('button', 'btn primary', 'Salvar');
        save.disabled = !t.dirty;
        save.addEventListener('click', () => saveTable(t.id));
        actions.appendChild(save);

        const del = el('button', 'btn danger', 'Apagar');
        del.addEventListener('click', () => deleteRow(t, renderList));
        actions.appendChild(del);
        head.appendChild(actions);
        form.appendChild(head);

        // Só os campos principais ficam à vista; o resto entra num bloco
        // recolhido. O que já veio certo do jogo original raramente precisa de
        // edição e só atrapalharia a leitura.
        const primary = t.meta.primary || [];
        const mainGrid = el('div', 'fields');
        const restGrid = el('div', 'fields');
        let restCount = 0;

        const build = (f) => {
            const idx = t.columns.indexOf(f.name);
            if (idx < 0) return null;      // campo do struct sem coluna no CSV
            const value = idx < row.length ? row[idx] : '';
            const refTarget = t.refs && t.refs[f.name];

            const onChange = (v) => {
                row[idx] = v;
                markDirty(t, form, renderList);
            };

            if (refTarget) return refField(t, f, value, refTarget, onChange);
            if (f.kind === 'array' && f.itemEnum) return enumChipsField(f, value, onChange);
            if (f.kind === 'array') return stringListField(f, value, onChange);
            return fieldFor(t, f, value, onChange);
        };

        fields.forEach((f) => {
            const node = build(f);
            if (!node) return;
            if (primary.indexOf(f.name) !== -1) {
                mainGrid.appendChild(node);
            } else {
                restGrid.appendChild(node);
                restCount++;
            }
        });

        const set = el('div', 'fieldset');
        set.appendChild(mainGrid);
        form.appendChild(set);

        if (restCount) {
            const details = el('details', 'more');
            const summary = el('summary');
            summary.textContent = 'Todos os campos  (' + restCount + ')';
            details.appendChild(summary);

            const body = el('div', 'more-body');
            // RowName é a identidade da linha: editável, mas fora do dia a dia
            body.appendChild(textField('RowName', 'row name', row[0], (v) => {
                row[0] = v;
                markDirty(t, form, renderList);
            }));
            body.appendChild(restGrid);
            details.appendChild(body);
            form.appendChild(details);
        }
    }

    function markDirty(t, form, renderList) {
        t.dirty = true;
        const save = form.querySelector('.btn.primary');
        if (save) save.disabled = false;
        const title = form.querySelector('.form-title');
        if (title) title.textContent = displayTitle(t, t.rows[t.sel]);
        renderList();
    }

    function wrap(label, type, wide) {
        const d = el('div', 'field' + (wide ? ' wide' : ''));
        const l = el('label');
        l.appendChild(document.createTextNode(label + ' '));
        l.appendChild(el('span', 'ftype', type));
        d.appendChild(l);
        return d;
    }

    function textField(label, type, value, onChange) {
        const d = wrap(label, type);
        const i = el('input');
        i.type = 'text';
        i.value = value;
        i.addEventListener('input', () => onChange(i.value));
        d.appendChild(i);
        return d;
    }

    /** Editor apropriado ao tipo declarado no struct C++. */
    function fieldFor(t, f, value, onChange) {
        const type = f.enum || f.cppType;

        if (f.kind === 'bool') {
            const d = wrap(f.name, type);
            const lab = el('label', 'toggle');
            const cb = el('input');
            cb.type = 'checkbox';
            cb.checked = value === 'True';
            const txt = el('span', null, cb.checked ? 'True' : 'False');
            cb.addEventListener('change', () => {
                txt.textContent = cb.checked ? 'True' : 'False';
                onChange(cb.checked ? 'True' : 'False');
            });
            lab.appendChild(cb);
            lab.appendChild(txt);
            d.appendChild(lab);
            return d;
        }

        if (f.kind === 'enum') {
            const d = wrap(f.name, type);
            const s = el('select');
            const values = (state.status.enums[f.enum] || []).slice();
            if (value && values.indexOf(value) === -1) values.unshift(value);
            values.forEach((v) => {
                const o = el('option', null, v);
                o.value = v;
                if (v === value) o.selected = true;
                s.appendChild(o);
            });
            s.addEventListener('change', () => onChange(s.value));
            d.appendChild(s);
            return d;
        }

        if (f.kind === 'int' || f.kind === 'float') {
            const d = wrap(f.name, type);
            const i = el('input');
            i.type = 'number';
            if (f.kind === 'float') i.step = 'any';
            i.value = value;
            i.addEventListener('input', () => onChange(i.value));
            d.appendChild(i);
            return d;
        }

        // Descrições são longas: textarea em linha inteira. Ancorado no FIM do
        // nome para não pegar EffectClass/EffectValue, que são valores curtos.
        const longText = /(Description|Notes|Text)$/i.test(f.name);
        const d = wrap(f.name, type, longText);
        if (longText) {
            const ta = el('textarea');
            ta.value = value;
            ta.addEventListener('input', () => onChange(ta.value));
            d.appendChild(ta);
        } else {
            const i = el('input');
            i.type = 'text';
            i.value = value;
            i.addEventListener('input', () => onChange(i.value));
            d.appendChild(i);
        }
        return d;
    }

    /**
     * TArray<Enum> — os valores viram chips que ligam/desligam. Melhor que um
     * campo de texto: não dá para digitar um elemento que não existe.
     */
    function enumChipsField(f, value, onChange) {
        const d = wrap(f.name, f.cppType, true);
        const box = el('div', 'chips');
        const chosen = parseArray(value);
        const all = state.status.enums[f.itemEnum] || [];

        all.forEach((v) => {
            if (v === 'None') return;                 // 'None' é ausência, não valor
            const c = el('button', 'chip' + (chosen.indexOf(v) !== -1 ? ' on' : ''), v);
            c.type = 'button';
            c.addEventListener('click', () => {
                const at = chosen.indexOf(v);
                if (at === -1) chosen.push(v); else chosen.splice(at, 1);
                c.classList.toggle('on', at === -1);
                onChange(makeArray(chosen));
            });
            box.appendChild(c);
        });

        d.appendChild(box);
        return d;
    }

    /** TArray<FString> — lista simples com adicionar e remover. */
    function stringListField(f, value, onChange) {
        const d = wrap(f.name, f.cppType, true);
        const box = el('div', 'reflist');
        const values = parseArray(value);

        function draw() {
            box.innerHTML = '';
            if (!values.length) box.appendChild(el('div', 'reflist-empty', 'Vazio.'));

            values.forEach((v, i) => {
                const r = el('div', 'refrow');
                r.appendChild(el('span', 'rname', v));
                const x = el('button', 'rdel', '✕');
                x.title = 'Remover';
                x.addEventListener('click', () => {
                    values.splice(i, 1);
                    onChange(makeArray(values));
                    draw();
                });
                r.appendChild(x);
                box.appendChild(r);
            });

            const add = el('div', 'addrow');
            const input = el('input');
            input.type = 'text';
            input.placeholder = 'Adicionar…';
            const ok = el('button', 'btn sm', 'Adicionar');
            const commit = () => {
                const v = input.value.trim();
                if (!v) return;
                values.push(v);
                onChange(makeArray(values));
                draw();
            };
            ok.addEventListener('click', commit);
            input.addEventListener('keydown', (e) => {
                if (e.key === 'Enter') { e.preventDefault(); commit(); }
            });
            add.appendChild(input);
            add.appendChild(ok);
            box.appendChild(add);
        }

        draw();
        d.appendChild(box);
        return d;
    }

    /**
     * Campo que referencia o RowName de outra tabela (estoque da loja, drop do inimigo).
     * Em vez de digitar "(healing_leaf,antidote)", mostra a lista com nome e
     * preço resolvidos e um botão que abre o seletor. Campo que não é array (um FName
     * só, como DropItem) guarda no máximo um item.
     */
    function refField(t, f, value, targetId, onChange) {
        const single = f.kind !== 'array';
        const d = wrap(f.name, (single ? 'item de ' : 'lista de ') + targetId, true);
        const box = el('div', 'reflist');
        let keys = refKeys(value);

        function push() { onChange(single ? (keys[0] || '') : makeArray(keys)); }

        function draw() {
            box.innerHTML = '';
            if (!keys.length) {
                box.appendChild(el('div', 'reflist-empty', 'Nenhum item.'));
            }
            keys.forEach((key, i) => {
                const ref = lookupRef(targetId, key);
                const r = el('div', 'refrow' + (ref ? '' : ' missing'));
                r.appendChild(el('span', 'rname', ref ? ref.name : key + '  (não existe em ' + targetId + ')'));
                r.appendChild(el('span', 'rkey', key));
                if (ref && ref.price) r.appendChild(el('span', 'rprice', ref.price + ' G'));

                const x = el('button', 'rdel', '✕');
                x.title = 'Remover';
                x.addEventListener('click', () => {
                    keys.splice(i, 1);
                    push();
                    draw();
                });
                r.appendChild(x);
                box.appendChild(r);
            });

            const add = el('button', 'btn', single && keys.length ? 'Trocar item' : '+ Adicionar item');
            add.style.marginTop = '8px';
            add.addEventListener('click', () => {
                openPicker(targetId, keys, (key) => {
                    if (single) keys = [key];
                    else keys.push(key);
                    push();
                    draw();
                });
            });
            box.appendChild(add);
        }

        draw();
        d.appendChild(box);
        return d;
    }

    function parseArray(cell) {
        cell = (cell || '').trim().replace(/^"|"$/g, '').trim();
        if (!cell.startsWith('(') || !cell.endsWith(')')) return [];
        const inner = cell.slice(1, -1).trim();
        return inner ? inner.split(',').map((s) => s.trim().replace(/^"|"$/g, '')).filter(Boolean) : [];
    }

    /** Keys citadas numa célula de referência: array "(a,b)" ou um FName só ("a"). */
    function refKeys(cell) {
        cell = (cell || '').trim();
        if (cell.startsWith('(')) return parseArray(cell);
        cell = cell.replace(/^"|"$/g, '').trim();
        return cell && cell !== 'None' ? [cell] : [];
    }

    function makeArray(values) {
        const clean = values.filter((v) => String(v).trim());
        return clean.length ? '(' + clean.join(',') + ')' : '';
    }

    // Resolve key -> {name, price} usando a tabela alvo (carrega sob demanda)
    const refCache = {};

    function lookupRef(targetId, key) {
        const t = state.tables[targetId] || refCache[targetId];
        if (!t) return null;
        const i = t.rows.findIndex((r) => r[0].trim() === key);
        if (i < 0) return null;
        return {
            name: colValue(t, t.rows[i], t.meta.title) || key,
            price: colValue(t, t.rows[i], t.meta.badge),
        };
    }

    async function ensureTable(id) {
        if (state.tables[id] || refCache[id]) return;
        try {
            refCache[id] = await api('table/' + id);
        } catch (e) {
            banner('Não consegui carregar ' + id + ' para resolver as referências: ' + e.message);
        }
    }

    // --- seletor de item ---

    function openPicker(targetId, already, onPick) {
        const src = state.tables[targetId] || refCache[targetId];
        if (!src) return toast('Tabela ' + targetId + ' não carregada', true);

        const box = $('#picker');
        const list = $('#picker-list');
        const search = $('#picker-search');
        $('#picker-title').textContent = 'Adicionar de ' + targetId;
        search.value = '';

        function draw() {
            const q = search.value.trim().toLowerCase();
            list.innerHTML = '';
            let n = 0;
            src.rows.forEach((r) => {
                const key = r[0].trim();
                const name = colValue(src, r, src.meta.title) || key;
                if (q && (name + ' ' + key).toLowerCase().indexOf(q) === -1) return;
                if (n++ > 400) return;      // lista longa: corta para não travar

                const has = already.indexOf(key) !== -1;
                const b = el('button', 'pick' + (has ? ' already' : ''));
                b.appendChild(el('span', 'pname', name));
                b.appendChild(el('span', 'pkey', key));
                const price = colValue(src, r, src.meta.badge);
                if (price) b.appendChild(el('span', 'pprice', price + (src.meta.badgeSuffix || '')));
                if (!has) {
                    b.addEventListener('click', () => {
                        onPick(key);
                        close();
                    });
                }
                list.appendChild(b);
            });
        }

        function close() {
            box.classList.remove('open');
            search.oninput = null;
        }

        search.oninput = draw;
        $('#picker-close').onclick = close;
        draw();
        box.classList.add('open');
        search.focus();
    }

    // --- salvar / apagar ---

    async function saveTable(id) {
        const t = state.tables[id];
        try {
            const res = await post('table/' + id + '/save', { rows: t.rows });
            t.dirty = false;
            state.status.findings = res.findings;
            refreshFindingsBadge();
            markNav();

            const view = document.querySelector('.view[data-view="' + id + '"]');
            const save = view && view.querySelector('.btn.primary');
            if (save) save.disabled = true;

            // A validação pode ter mudado — se a tela dela existe, redesenha
            const val = document.querySelector('.view[data-view="validation"]');
            if (val) renderValidation(val);

            toast('Salvo · ' + res.rowCount + ' linhas' + (res.backup ? ' · backup em ' + res.backup : ''));
        } catch (e) {
            toast('Falhou ao salvar: ' + e.message, true);
        }
    }

    async function deleteRow(t, renderList) {
        const row = t.rows[t.sel];
        const key = row[0].trim();

        // Avisa se a linha está em uso antes de deixar apagar
        const used = usedBy(t.id, key);
        const extra = used.length
            ? '\n\nEm uso por: ' + used.map((u) => u.table + ' · ' + u.row + ' (' + u.field + ')').join(', ')
            : '';

        const ok = await confirmDialog(
            'Apagar ' + displayTitle(t, row) + '?',
            'A linha "' + key + '" sai de ' + t.id + '. Só vale depois de Salvar.' + extra,
            'Apagar');
        if (!ok) return;

        t.rows.splice(t.sel, 1);
        if (t.sel >= t.rows.length) t.sel = t.rows.length - 1;
        t.dirty = true;

        const view = document.querySelector('.view[data-view="' + t.id + '"]');
        const form = view.querySelector('.form');
        renderList();
        form.innerHTML = '';
        if (t.sel >= 0) buildForm(t, form, renderList);
        markNav();
        toast('Removido — lembre de salvar');
    }

    /** Quem referencia esta linha (varre as tabelas já carregadas). */
    function usedBy(tableId, key) {
        const hits = [];
        Object.keys(state.tables).forEach((srcId) => {
            const src = state.tables[srcId];
            if (!src.refs) return;
            Object.keys(src.refs).forEach((field) => {
                if (src.refs[field] !== tableId) return;
                const idx = src.columns.indexOf(field);
                if (idx < 0) return;
                src.rows.forEach((r) => {
                    if (idx < r.length && refKeys(r[idx]).indexOf(key) !== -1) {
                        hits.push({ table: srcId, row: r[0].trim(), field: field });
                    }
                });
            });
        });
        return hits;
    }

    // ====================================================================
    // ONDAS DE BATALHA (JSON do AJRPGBattleWaves)
    // ====================================================================
    //
    // Um arquivo = um conjunto de ondas (o Muscle Dome). A lista da esquerda são
    // os arquivos da pasta de ondas do projeto Unreal; a ficha edita um deles em
    // memória e só grava no Salvar (com backup em Saved/ do projeto).

    async function openWaves(view) {
        view.innerHTML = '';
        await Promise.all([ensureTable('DT_Enemies'), ensureTable('DT_Characters')]);

        const ws = state.waves;
        const list = el('div', 'reclist');
        const top = el('div', 'reclist-top');
        const info = el('div', 'reclist-info');
        const count = el('span');
        info.appendChild(count);
        const addBtn = el('button', 'btn', '+ Novo');
        addBtn.style.padding = '5px 11px';
        addBtn.style.fontSize = '10px';
        info.appendChild(addBtn);
        top.appendChild(info);
        list.appendChild(top);
        const items = el('div', 'reclist-items');
        list.appendChild(items);
        view.appendChild(list);

        const form = el('div', 'form');
        view.appendChild(form);

        async function refreshListing() {
            try {
                ws.listing = await api('waves');
            } catch (e) {
                banner('Não consegui listar as ondas: ' + e.message);
                ws.listing = { files: [], dirOk: false, dir: '' };
            }
        }

        function renderList() {
            items.innerHTML = '';
            const files = ws.listing.files;
            count.textContent = files.length + (files.length === 1 ? ' arquivo' : ' arquivos');
            if (!ws.listing.dirOk) {
                const p = el('p', 'muted', 'A pasta ainda não existe: ' + (ws.listing.dir || '(projeto Unreal não configurado)')
                    + '. "+ Novo" cria.');
                p.style.padding = '10px';
                items.appendChild(p);
            }
            files.forEach((f) => {
                const active = ws.current && ws.current.file === f.file;
                const b = el('button', 'rec' + (active ? ' active' : ''));
                const main = el('div', 'rec-main');
                main.appendChild(el('div', 'rec-title', f.name + (active && ws.current.dirty ? ' •' : '')));
                main.appendChild(el('div', 'rec-sub', f.error ? f.file + '  ·  ' + f.error : f.file));
                b.appendChild(main);
                b.appendChild(el('span', 'rec-badge', f.error ? '!' : f.waves + (f.waves === 1 ? ' onda' : ' ondas')));
                b.addEventListener('click', () => select(f.file));
                items.appendChild(b);
            });
        }

        async function select(file) {
            if (ws.current && ws.current.dirty && ws.current.file !== file) {
                const ok = await confirmDialog('Descartar alterações?',
                    ws.current.file + ' tem alterações não salvas.', 'Descartar');
                if (!ok) return;
            }
            try {
                const data = await api('waves/' + file);
                ws.current = { file: file, data: data, dirty: false };
            } catch (e) {
                ws.current = null;
                toast('Não consegui abrir ' + file + ': ' + e.message, true);
            }
            renderList();
            renderForm();
        }

        function dirty() {
            ws.current.dirty = true;
            const save = form.querySelector('.btn.primary');
            if (save) save.disabled = false;
            renderList();
        }

        function renderForm() {
            form.innerHTML = '';
            if (!ws.current) {
                const p = el('p', 'muted', 'Escolha um arquivo de ondas, ou crie um com "+ Novo".');
                p.style.marginTop = '30px';
                form.appendChild(p);
                return;
            }
            const d = ws.current.data;

            const head = el('div', 'form-head');
            const left = el('div');
            left.appendChild(el('div', 'form-title', d.name || ws.current.file));
            left.appendChild(el('div', 'form-key', 'WavesFile no Unreal:  ' + (d.unrealPath || ws.current.file)));
            head.appendChild(left);
            const actions = el('div', 'form-actions');
            const save = el('button', 'btn primary', 'Salvar');
            save.disabled = !ws.current.dirty;
            save.addEventListener('click', saveWaves);
            actions.appendChild(save);
            head.appendChild(actions);
            form.appendChild(head);

            // --- o conjunto ---
            const grid = el('div', 'fields');
            grid.appendChild(textField('Nome', 'texto', d.name || '', (v) => { d.name = v; dirty(); }));
            grid.appendChild(numberField('Espera inicial', 'segundos', d.startDelay, true, (v) => { d.startDelay = v; dirty(); }));
            grid.appendChild(numberField('Espera entre ondas', 'segundos', d.delayBetweenWaves, true, (v) => { d.delayBetweenWaves = v; dirty(); }));
            grid.appendChild(toggleField('Loop', 'depois da última, volta à primeira', !!d.loop, (v) => { d.loop = v; dirty(); }));
            grid.appendChild(partyField(d));
            const set = el('div', 'fieldset');
            set.appendChild(grid);
            form.appendChild(set);

            // --- as ondas ---
            const waves = el('div', 'waves');
            (d.waves || []).forEach((w, i) => waves.appendChild(waveCard(d, w, i)));
            form.appendChild(waves);

            const add = el('button', 'btn', '+ Onda');
            add.addEventListener('click', () => {
                d.waves.push({ name: 'Onda ' + (d.waves.length + 1), enemiesMin: 1, enemiesMax: 1, enemies: [] });
                dirty();
                renderForm();
            });
            form.appendChild(add);
        }

        /** Party: chips na ordem em que são ligados (a ordem é a formação; o primeiro é o líder). */
        function partyField(d) {
            const f = wrap('Party', 'ordem = formação · vazio = a party ativa do save', true);
            const box = el('div', 'chips');
            const chars = state.tables.DT_Characters || refCache.DT_Characters;
            const keys = chars ? chars.rows.map((r) => r[0].trim()) : [];
            d.party = d.party || [];
            d.party.forEach((k) => { if (keys.indexOf(k) === -1) keys.push(k); });

            keys.forEach((k) => {
                const at = d.party.indexOf(k);
                const c = el('button', 'chip' + (at !== -1 ? ' on' : ''), at !== -1 ? (at + 1) + '. ' + k : k);
                c.type = 'button';
                c.addEventListener('click', () => {
                    const i = d.party.indexOf(k);
                    if (i === -1) d.party.push(k); else d.party.splice(i, 1);
                    dirty();
                    f.replaceWith(partyField(d));
                });
                box.appendChild(c);
            });
            f.appendChild(box);
            return f;
        }

        function waveCard(d, w, i) {
            const card = el('div', 'wave-card');

            const head = el('div', 'wave-head');
            head.appendChild(el('span', 'wave-num', 'Onda ' + (i + 1)));
            const name = el('input');
            name.type = 'text';
            name.placeholder = 'nome (aparece na tela)';
            name.value = w.name || '';
            name.addEventListener('input', () => { w.name = name.value; dirty(); });
            head.appendChild(name);

            const tool = (label, title, fn, disabled) => {
                const b = el('button', 'btn sm', label);
                b.title = title;
                b.disabled = !!disabled;
                b.addEventListener('click', () => { fn(); dirty(); renderForm(); });
                head.appendChild(b);
            };
            tool('↑', 'Subir', () => { d.waves.splice(i - 1, 0, d.waves.splice(i, 1)[0]); }, i === 0);
            tool('↓', 'Descer', () => { d.waves.splice(i + 1, 0, d.waves.splice(i, 1)[0]); }, i === d.waves.length - 1);
            tool('Duplicar', 'Duplicar esta onda', () => { d.waves.splice(i + 1, 0, JSON.parse(JSON.stringify(w))); });
            tool('✕', 'Apagar esta onda', () => { d.waves.splice(i, 1); });
            card.appendChild(head);

            const totals = el('div', 'wave-totals');
            totals.appendChild(el('span', null, 'Inimigos por luta'));
            totals.appendChild(smallNumber(w.enemiesMin, 1, (v) => { w.enemiesMin = v; dirty(); summary(); }));
            totals.appendChild(el('span', null, 'a'));
            totals.appendChild(smallNumber(w.enemiesMax, 1, (v) => { w.enemiesMax = v; dirty(); summary(); }));
            const hint = el('span', 'wave-hint');
            totals.appendChild(hint);
            card.appendChild(totals);

            const table = el('div', 'wave-enemies');
            const header = el('div', 'wave-row wave-row-head');
            ['Inimigo', 'Lv mín', 'Lv máx', 'Qtd mín', 'Qtd máx', 'Peso', 'Sempre entra', ''].forEach((h) => header.appendChild(el('span', null, h)));
            table.appendChild(header);
            w.enemies = w.enemies || [];
            w.enemies.forEach((e, j) => table.appendChild(enemyRow(w, e, j)));
            card.appendChild(table);

            const add = el('button', 'btn sm', '+ Inimigo');
            add.addEventListener('click', () => {
                openPicker('DT_Enemies', w.enemies.map((x) => x.key), (key) => {
                    w.enemies.push({ key: key, levelMin: 1, levelMax: 1, countMin: 1, countMax: 1, weight: 1,
                                     alwaysJoins: w.enemies.length === 0 });
                    dirty();
                    renderForm();
                });
            });
            card.appendChild(add);

            /** O que esta onda pode sortear, e o que vai dar errado no jogo. */
            function summary() {
                const problems = [];
                const fixed = w.enemies.filter((e) => e.alwaysJoins).reduce((n, e) => n + (e.countMin || 0), 0);
                const drawable = w.enemies.some((e) => !e.alwaysJoins && (e.weight || 0) > 0 && (e.countMax || 0) > 0);
                if (!w.enemies.length) problems.push('sem inimigo nenhum');
                else if (!fixed && !drawable) problems.push('ninguém entra: marque "Sempre entra" ou dê peso a alguém');
                if ((w.enemiesMax || 1) < (w.enemiesMin || 1)) problems.push('máximo menor que o mínimo');
                hint.textContent = problems.length ? problems.join(' · ')
                    : 'sai de ' + (w.enemiesMin || 1) + ' a ' + Math.max(w.enemiesMin || 1, w.enemiesMax || 1) + ' inimigo(s)'
                      + (fixed ? ' · ' + fixed + ' fixo(s)' : '');
                hint.classList.toggle('bad', problems.length > 0);
            }
            summary();
            return card;
        }

        function enemyRow(w, e, j) {
            const row = el('div', 'wave-row');
            const t = state.tables.DT_Enemies || refCache.DT_Enemies;
            const idx = t ? t.rows.findIndex((r) => r[0].trim() === e.key) : -1;
            const who = el('span', 'wave-who');
            if (idx < 0) {
                row.classList.add('missing');
                who.appendChild(el('span', 'rname', e.key + '  (não existe no DT_Enemies)'));
            } else {
                const r = t.rows[idx];
                who.appendChild(el('span', 'rname', (colValue(t, r, 'DisplayName') || e.key)));
                who.appendChild(el('span', 'rkey', e.key + ' · ' + colValue(t, r, 'BaseHP') + ' HP'));
                if (!colValue(t, r, 'Mesh')) {
                    const warn = el('span', 'wave-nomodel', 'sem modelo');
                    warn.title = 'Sem Mesh no DT_Enemies: na luta aparece só a cápsula';
                    who.appendChild(warn);
                }
            }
            row.appendChild(who);

            const num = (field, low) => row.appendChild(smallNumber(e[field], low, (v) => { e[field] = v; dirty(); }));
            num('levelMin', 1);
            num('levelMax', 1);
            num('countMin', 0);
            num('countMax', 0);
            num('weight', 0);

            const cb = el('input');
            cb.type = 'checkbox';
            cb.checked = !!e.alwaysJoins;
            cb.title = 'Entra em toda luta desta onda (Qtd mín..máx dele)';
            cb.addEventListener('change', () => { e.alwaysJoins = cb.checked; dirty(); renderForm(); });
            const cbWrap = el('span', 'wave-check');
            cbWrap.appendChild(cb);
            row.appendChild(cbWrap);

            const x = el('button', 'rdel', '✕');
            x.title = 'Remover';
            x.addEventListener('click', () => { w.enemies.splice(j, 1); dirty(); renderForm(); });
            row.appendChild(x);
            return row;
        }

        async function saveWaves() {
            const c = ws.current;
            try {
                const res = await post('waves/' + c.file + '/save', c.data);
                c.dirty = false;
                c.data.unrealPath = res.unrealPath;
                await refreshListing();
                renderList();
                renderForm();
                toast('Salvo · ' + c.file + (res.backup ? ' · backup em Saved/LegaiaStudio' : ''));
            } catch (e) {
                toast('Falhou ao salvar: ' + e.message, true);
            }
        }

        addBtn.addEventListener('click', async () => {
            const name = prompt('Nome do arquivo (ex.: muscle_dome):', '');
            if (name === null) return;
            const file = name.trim().replace(/\.json$/i, '') + '.json';
            if (!/^[\w\-]+\.json$/.test(file)) return toast('Use só letras, números, _ e -', true);
            try {
                await post('waves/' + file + '/new', {});
            } catch (e) {
                return toast('Não consegui criar: ' + e.message, true);
            }
            await refreshListing();
            await select(file);
            toast('Criado: ' + file);
        });

        await refreshListing();
        renderList();
        if (ws.current) renderForm();
        else if (ws.listing.files.length) await select(ws.listing.files[0].file);
        else renderForm();
    }

    function numberField(label, type, value, isFloat, onChange) {
        const d = wrap(label, type);
        const i = el('input');
        i.type = 'number';
        i.min = '0';
        if (isFloat) i.step = 'any';
        i.value = value === undefined || value === null ? '' : value;
        i.addEventListener('input', () => onChange(isFloat ? parseFloat(i.value) || 0 : parseInt(i.value, 10) || 0));
        d.appendChild(i);
        return d;
    }

    function toggleField(label, type, value, onChange) {
        const d = wrap(label, type);
        const lab = el('label', 'toggle');
        const cb = el('input');
        cb.type = 'checkbox';
        cb.checked = value;
        const txt = el('span', null, value ? 'Sim' : 'Não');
        cb.addEventListener('change', () => {
            txt.textContent = cb.checked ? 'Sim' : 'Não';
            onChange(cb.checked);
        });
        lab.appendChild(cb);
        lab.appendChild(txt);
        d.appendChild(lab);
        return d;
    }

    function smallNumber(value, low, onChange) {
        const i = el('input', 'num-sm');
        i.type = 'number';
        i.min = String(low);
        i.value = value === undefined || value === null ? low : value;
        i.addEventListener('input', () => {
            const v = parseInt(i.value, 10);
            onChange(isNaN(v) ? low : Math.max(low, v));
        });
        return i;
    }

    // ====================================================================
    // BUILDS
    // ====================================================================

    async function runBuild(spec) {
        const dirty = Object.keys(state.tables).filter((id) => state.tables[id].dirty);

        let text = spec.note || '';
        if (dirty.length) {
            text += '\n\nAtenção: há alterações não salvas em '
                  + dirty.join(', ') + '. O build usa o que está em disco.';
        }

        const ok = await confirmDialog('Rodar ' + spec.label + '?', text, 'Rodar', false);
        if (!ok) return;

        try {
            await post('build/' + spec.key);
        } catch (e) {
            return toast(e.message, true);
        }

        $('#log-title').textContent = spec.label;
        $('#log-body').textContent = '';
        $('#log-state').textContent = 'rodando…';
        $('#logpanel').classList.add('open');
        $('#shell').classList.add('busy');

        let from = 0;
        clearInterval(state.buildTimer);
        state.buildTimer = setInterval(async () => {
            let res;
            try {
                res = await api('build/' + spec.key + '/log?from=' + from);
            } catch (e) {
                clearInterval(state.buildTimer);
                $('#log-state').textContent = 'erro: ' + e.message;
                $('#shell').classList.remove('busy');
                return;
            }
            if (res.lines.length) {
                const body = $('#log-body');
                const atBottom = body.scrollTop + body.clientHeight >= body.scrollHeight - 40;
                body.textContent += res.lines.join('');
                if (atBottom) body.scrollTop = body.scrollHeight;
                from = res.next;
            }
            if (res.done) {
                clearInterval(state.buildTimer);
                $('#shell').classList.remove('busy');
                const ok = res.code === 0;
                $('#log-state').textContent = ok ? 'concluído' : 'falhou (código ' + res.code + ')';
                $('#log-state').style.color = ok ? 'var(--ok)' : 'var(--danger)';
                toast(spec.label + (ok ? ' concluído' : ' falhou'), !ok);
            }
        }, 500);
    }

    $('#log-close').addEventListener('click', () => {
        $('#logpanel').classList.remove('open');
    });

    // ====================================================================
    // BOOT
    // ====================================================================

    (async function () {
        try {
            state.status = await api('status');
        } catch (e) {
            banner('Não consegui falar com o studio.py — ' + e.message);
            return;
        }

        buildSidebar();

        // A DT_Items é usada para resolver o estoque das lojas, então carrega
        // antes mesmo de o usuário abrir a aba dela.
        await ensureTable('DT_Items');

        go('overview');
    })();
})();
