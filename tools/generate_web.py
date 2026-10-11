#!/usr/bin/env python3
"""
Generate web/index.html for the FM-106 Web MIDI Librarian & Front-Panel Editor.
Includes authentic FM-106 UI styling, full bidirectional Web MIDI SysEx,
128 factory presets embedded, library manager, and audition keyboard.
"""
import json
import os

with open("tests/compact_factory_presets.json", "r") as f:
    compact_presets = json.load(f)

presets_js_code = json.dumps(compact_presets)

html_template = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>FM-106 // Web Librarian & Synthesizer Editor</title>
<style>
:root {{
    --bg-synth: #1c1d22;
    --panel-dark: #121316;
    --panel-mid: #252830;
    --panel-light: #323642;
    --accent-red: #e62c1e;
    --accent-red-hover: #ff4133;
    --accent-blue: #00a4e4;
    --accent-amber: #f89e1b;
    --accent-green: #2ecc71;
    --text-white: #f5f6f8;
    --text-muted: #8e95a5;
    --fader-cap: #e4e7ed;
    --fader-stripe: #e62c1e;
    --track-bg: #0b0c0e;
    --font-ui: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
    --font-mono: "SF Mono", Monaco, Inconsolata, "Courier New", monospace;
}}

* {{
    box-sizing: border-box;
    margin: 0;
    padding: 0;
    user-select: none;
    -webkit-user-select: none;
}}

body {{
    background-color: #0c0d10;
    color: var(--text-white);
    font-family: var(--font-ui);
    display: flex;
    flex-direction: column;
    min-height: 100vh;
    overflow-x: hidden;
}}

/* Top Synth Header */
header.synth-header {{
    background: linear-gradient(180deg, #2b2e37 0%, #1c1d22 100%);
    border-bottom: 3px solid var(--accent-red);
    box-shadow: 0 4px 16px rgba(0,0,0,0.6);
    padding: 10px 20px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    flex-wrap: wrap;
    gap: 15px;
    z-index: 100;
}}

.brand-section {{
    display: flex;
    align-items: center;
    gap: 14px;
}}

.brand-badge {{
    background: transparent;
    color: var(--accent-blue);
    font-weight: 900;
    font-size: 24px;
    letter-spacing: 3px;
    padding: 0;
    text-shadow: 0 1px 3px black;
}}

.model-title {{
    font-size: 16px;
    font-weight: 800;
    letter-spacing: 2px;
    color: var(--text-white);
    text-shadow: 0 1px 2px black;
}}

.model-subtitle {{
    font-size: 10px;
    color: var(--text-muted);
    font-weight: 600;
    letter-spacing: 1.5px;
    text-transform: uppercase;
}}

.midi-toolbar {{
    display: flex;
    align-items: center;
    gap: 12px;
    background: var(--panel-dark);
    padding: 6px 14px;
    border-radius: 4px;
    border: 1px solid #2d3039;
}}

.led-indicator {{
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background-color: #444;
    box-shadow: inset 0 1px 2px rgba(0,0,0,0.6);
    display: inline-block;
    transition: all 0.15s ease;
}}

.led-indicator.active {{
    background-color: var(--accent-green);
    box-shadow: 0 0 8px var(--accent-green);
}}

.led-indicator.amber.active {{
    background-color: var(--accent-amber);
    box-shadow: 0 0 8px var(--accent-amber);
}}

.led-indicator.red.active {{
    background-color: var(--accent-red);
    box-shadow: 0 0 8px var(--accent-red);
}}

select.synth-select {{
    background: #1a1b20;
    color: var(--text-white);
    border: 1px solid #3c404d;
    border-radius: 3px;
    padding: 5px 8px;
    font-size: 12px;
    font-family: var(--font-ui);
    cursor: pointer;
    outline: none;
}}

select.synth-select:focus {{
    border-color: var(--accent-blue);
}}

button.synth-btn {{
    background: linear-gradient(180deg, #3c404e 0%, #282a33 100%);
    color: white;
    border: 1px solid #4a5061;
    padding: 6px 12px;
    font-size: 11px;
    font-weight: 700;
    letter-spacing: 1px;
    text-transform: uppercase;
    border-radius: 3px;
    cursor: pointer;
    box-shadow: 0 2px 4px rgba(0,0,0,0.4);
    transition: all 0.1s ease;
    display: inline-flex;
    align-items: center;
    gap: 6px;
}}

button.synth-btn:hover {{
    background: linear-gradient(180deg, #4c5264 0%, #323541 100%);
    border-color: #656d84;
}}

button.synth-btn:active {{
    transform: translateY(1px);
    box-shadow: 0 1px 2px rgba(0,0,0,0.6);
}}

button.synth-btn.primary {{
    background: linear-gradient(180deg, var(--accent-red-hover) 0%, var(--accent-red) 100%);
    border-color: #b71c1c;
}}

button.synth-btn.primary:hover {{
    background: linear-gradient(180deg, #ff574d 0%, #ea3123 100%);
}}

button.synth-btn.accent {{
    background: linear-gradient(180deg, #00b4fa 0%, #0087bd 100%);
    border-color: #006b96;
}}

button.synth-btn.amber {{
    background: linear-gradient(180deg, #ffad33 0%, #e68500 100%);
    border-color: #b36700;
}}

/* Main Workspace Layout */
.workspace {{
    display: grid;
    grid-template-columns: 340px 1fr;
    flex: 1;
    overflow: hidden;
}}

@media (max-width: 1050px) {{
    .workspace {{
        grid-template-columns: 1fr;
    }}
}}

/* Left Sidebar: Library Manager */
aside.library-sidebar {{
    background: #141519;
    border-right: 2px solid #282a32;
    display: flex;
    flex-direction: column;
    overflow: hidden;
}}

.sidebar-header {{
    background: #1a1b21;
    padding: 12px 14px;
    border-bottom: 1px solid #282a32;
    display: flex;
    flex-direction: column;
    gap: 10px;
}}

.bank-tabs {{
    display: flex;
    gap: 4px;
    background: #0d0e11;
    padding: 3px;
    border-radius: 4px;
    border: 1px solid #22252c;
}}

.bank-tab {{
    flex: 1;
    text-align: center;
    padding: 6px 0;
    font-size: 10px;
    font-weight: 800;
    letter-spacing: 0.5px;
    color: var(--text-muted);
    cursor: pointer;
    border-radius: 3px;
    transition: all 0.15s ease;
}}

.bank-tab.active {{
    background: var(--accent-red);
    color: white;
    box-shadow: 0 2px 4px rgba(0,0,0,0.5);
}}

.bank-tab.user-tab {{
    color: #f59e0b;
}}

.bank-tab.user-tab.active {{
    background: #d97706;
    color: white;
}}

.search-bar {{
    display: flex;
    gap: 6px;
}}

input.synth-input {{
    background: #0d0e11;
    border: 1px solid #2c2f38;
    color: var(--text-white);
    padding: 6px 10px;
    border-radius: 3px;
    font-size: 12px;
    font-family: var(--font-ui);
    outline: none;
    width: 100%;
}}

input.synth-input:focus {{
    border-color: var(--accent-blue);
}}

.library-actions-bar {{
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 6px;
    padding: 8px 14px;
    background: #18191f;
    border-bottom: 1px solid #282a32;
}}

.preset-list-container {{
    flex: 1;
    overflow-y: auto;
    padding: 8px;
}}

.preset-list-container::-webkit-scrollbar {{
    width: 6px;
}}

.preset-list-container::-webkit-scrollbar-thumb {{
    background: #2f323c;
    border-radius: 3px;
}}

.preset-item {{
    display: flex;
    align-items: center;
    justify-content: space-between;
    padding: 7px 10px;
    margin-bottom: 4px;
    background: #1a1c22;
    border: 1px solid #252830;
    border-radius: 3px;
    cursor: pointer;
    transition: all 0.1s ease;
}}

.preset-item:hover {{
    background: #252832;
    border-color: #383d4a;
}}

.preset-item.selected {{
    background: #282d3b;
    border-color: var(--accent-blue);
    box-shadow: inset 2px 0 0 var(--accent-blue);
}}

.preset-slot-badge {{
    font-family: var(--font-mono);
    font-size: 11px;
    font-weight: 700;
    background: #101115;
    color: var(--accent-amber);
    padding: 2px 6px;
    border-radius: 2px;
    border: 1px solid #2a2c36;
    min-width: 38px;
    text-align: center;
}}

.preset-item-name {{
    flex: 1;
    font-size: 12px;
    font-weight: 600;
    margin-left: 10px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}}

.preset-quick-actions {{
    display: flex;
    gap: 4px;
    opacity: 0.7;
}}

.preset-item:hover .preset-quick-actions {{
    opacity: 1;
}}

.mini-btn {{
    background: #121317;
    border: 1px solid #2d303a;
    color: var(--text-muted);
    padding: 2px 6px;
    font-size: 10px;
    font-weight: 700;
    border-radius: 2px;
    cursor: pointer;
}}

.mini-btn:hover {{
    background: var(--accent-blue);
    color: white;
    border-color: var(--accent-blue);
}}

/* Hardware Sync & Storage Bar */
.hardware-sync-bar {{
    background: #101114;
    border-top: 1px solid #282a32;
    padding: 10px 14px;
    display: flex;
    flex-direction: column;
    gap: 8px;
}}

.sync-status-text {{
    font-size: 11px;
    font-family: var(--font-mono);
    color: var(--text-muted);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}}

/* Right Panel: FM-106 Front Panel Synth */
main.synth-panel {{
    background: radial-gradient(circle at 50% 30%, #23252d 0%, #15161a 100%);
    padding: 16px 20px;
    overflow-y: auto;
    display: flex;
    flex-direction: column;
    gap: 16px;
}}

/* Active Patch Ribbon */
.patch-banner {{
    background: #121317;
    border: 1px solid #2a2d36;
    border-left: 5px solid var(--accent-red);
    border-radius: 4px;
    padding: 12px 18px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    flex-wrap: wrap;
    gap: 15px;
    box-shadow: 0 4px 12px rgba(0,0,0,0.4);
}}

.patch-slot-large {{
    font-family: var(--font-mono);
    font-size: 24px;
    font-weight: 800;
    color: var(--accent-amber);
    background: #08090b;
    padding: 4px 14px;
    border-radius: 3px;
    border: 1px solid #333644;
    text-shadow: 0 0 10px rgba(248, 158, 27, 0.4);
    display: inline-block;
}}

.patch-name-editor {{
    display: flex;
    align-items: center;
    gap: 10px;
    flex: 1;
    max-width: 400px;
}}

.patch-name-input {{
    font-size: 18px;
    font-weight: 700;
    letter-spacing: 1px;
    padding: 6px 12px;
    background: #0b0c0f;
    border: 1px solid #3a3e4e;
    color: #fff;
    border-radius: 3px;
    width: 100%;
}}

.patch-name-input:focus {{
    border-color: var(--accent-blue);
    box-shadow: 0 0 8px rgba(0, 164, 228, 0.3);
}}

.patch-actions {{
    display: flex;
    align-items: center;
    gap: 8px;
}}

/* Synth Sections Grid */
.synth-sections {{
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(260px, 1fr));
    gap: 12px;
}}

.synth-module {{
    background: #181a20;
    border: 1px solid #2a2d37;
    border-top: 3px solid var(--accent-blue);
    border-radius: 4px;
    padding: 12px 10px;
    display: flex;
    flex-direction: column;
    box-shadow: inset 0 1px 0 rgba(255,255,255,0.05), 0 3px 8px rgba(0,0,0,0.3);
}}

.synth-module.red-top {{
    border-top-color: var(--accent-red);
}}

.synth-module.amber-top {{
    border-top-color: var(--accent-amber);
}}

.module-title {{
    font-size: 12px;
    font-weight: 800;
    letter-spacing: 2px;
    color: #cad0df;
    text-transform: uppercase;
    text-align: center;
    margin-bottom: 12px;
    padding-bottom: 4px;
    border-bottom: 1px solid #2a2d37;
}}

.module-controls {{
    display: flex;
    justify-content: space-around;
    align-items: flex-end;
    gap: 8px;
    flex: 1;
    padding: 4px 0;
}}

/* Vertical Synth Fader */
.fader-control {{
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 6px;
    width: 38px;
}}

.fader-label {{
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 0.5px;
    color: #9aa2b5;
    text-transform: uppercase;
    text-align: center;
    height: 18px;
    display: flex;
    align-items: center;
    justify-content: center;
}}

.fader-val {{
    font-family: var(--font-mono);
    font-size: 10px;
    color: var(--accent-amber);
    background: #090a0d;
    padding: 1px 4px;
    border-radius: 2px;
    border: 1px solid #272a33;
    min-width: 26px;
    text-align: center;
}}

.fader-track-wrap {{
    position: relative;
    width: 26px;
    height: 120px;
    display: flex;
    justify-content: center;
    align-items: center;
}}

.fader-track-ticks {{
    position: absolute;
    left: 2px;
    right: 2px;
    top: 6px;
    bottom: 6px;
    background: repeating-linear-gradient(
        180deg,
        rgba(255,255,255,0.2) 0px,
        rgba(255,255,255,0.2) 1px,
        transparent 1px,
        transparent 10.8px
    );
    pointer-events: none;
}}

.fader-slot {{
    position: absolute;
    width: 4px;
    height: 100%;
    background: #000;
    border-radius: 2px;
    box-shadow: inset 0 0 2px black;
}}

/* Hidden native range rotated vertical */
input[type=range].fader-input {{
    position: absolute;
    width: 120px;
    height: 26px;
    margin: 0;
    opacity: 0;
    cursor: ns-resize;
    transform: rotate(-90deg);
    z-index: 10;
}}

/* Visual Custom Fader Cap */
.fader-cap {{
    position: absolute;
    width: 22px;
    height: 14px;
    background: linear-gradient(180deg, #e6e8ec 0%, #b2b6c1 50%, #898e9b 100%);
    border: 1px solid #4a4f5c;
    border-radius: 2px;
    box-shadow: 0 2px 5px rgba(0,0,0,0.7), inset 0 1px 0 rgba(255,255,255,0.8);
    pointer-events: none;
    display: flex;
    align-items: center;
    justify-content: center;
    transition: transform 0.04s ease;
}}

.fader-cap::after {{
    content: '';
    width: 100%;
    height: 2px;
    background-color: var(--fader-stripe);
    box-shadow: 0 0 2px var(--fader-stripe);
}}

/* Switch & Toggle Controls */
.switch-control {{
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 8px;
    width: 46px;
}}

.switch-options {{
    display: flex;
    flex-direction: column;
    gap: 3px;
    background: #0d0e12;
    padding: 3px;
    border-radius: 4px;
    border: 1px solid #252833;
    width: 100%;
}}

.switch-opt {{
    font-size: 9px;
    font-weight: 700;
    padding: 4px 2px;
    text-align: center;
    color: var(--text-muted);
    border-radius: 2px;
    cursor: pointer;
    transition: all 0.1s ease;
}}

.switch-opt:hover {{
    color: white;
}}

.switch-opt.active {{
    background: var(--accent-red);
    color: white;
    box-shadow: 0 1px 3px rgba(0,0,0,0.5);
}}

.switch-opt.blue.active {{
    background: var(--accent-blue);
}}

/* Chorus Push Buttons */
.chorus-buttons {{
    display: flex;
    flex-direction: column;
    gap: 6px;
    width: 100%;
}}

.chorus-btn {{
    background: #14151b;
    border: 1px solid #2e313d;
    color: #a2a8b8;
    padding: 8px 10px;
    font-size: 11px;
    font-weight: 700;
    border-radius: 3px;
    cursor: pointer;
    display: flex;
    align-items: center;
    justify-content: space-between;
    transition: all 0.1s ease;
}}

.chorus-btn:hover {{
    background: #20232b;
    color: white;
}}

.chorus-btn.active {{
    border-color: var(--accent-amber);
    background: #27221a;
    color: #fff;
}}

.chorus-btn.active .led-indicator {{
    background-color: var(--accent-amber);
    box-shadow: 0 0 8px var(--accent-amber);
}}

/* Playable Virtual Keyboard */
footer.keyboard-footer {{
    background: #111216;
    border-top: 3px solid #282a33;
    padding: 10px 20px;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 10px;
    box-shadow: 0 -4px 16px rgba(0,0,0,0.6);
    z-index: 90;
}}

.keyboard-controls {{
    display: flex;
    align-items: center;
    justify-content: space-between;
    width: 100%;
    max-width: 900px;
    font-size: 12px;
}}

.octave-controls {{
    display: flex;
    align-items: center;
    gap: 8px;
}}

.keyboard-keys {{
    position: relative;
    display: flex;
    height: 110px;
    max-width: 900px;
    width: 100%;
    background: #000;
    border: 3px solid #1a1b20;
    border-radius: 4px;
    overflow: hidden;
    box-shadow: 0 6px 16px rgba(0,0,0,0.8);
}}

.key {{
    position: relative;
    cursor: pointer;
    user-select: none;
}}

.white-key {{
    flex: 1;
    background: linear-gradient(180deg, #f7f7f7 0%, #ebebeb 85%, #d1d1d1 100%);
    border-right: 1px solid #bbb;
    border-radius: 0 0 4px 4px;
    z-index: 1;
    box-shadow: inset 0 -2px 5px rgba(0,0,0,0.15);
}}

.white-key:last-child {{
    border-right: none;
}}

.white-key:active, .white-key.active {{
    background: linear-gradient(180deg, #d8d8d8 0%, #cccccc 100%);
    box-shadow: inset 0 2px 4px rgba(0,0,0,0.4);
}}

.black-key {{
    position: absolute;
    width: 4.2%;
    height: 65%;
    background: linear-gradient(180deg, #2a2a2a 0%, #151515 80%, #050505 100%);
    border-radius: 0 0 3px 3px;
    z-index: 2;
    box-shadow: 0 3px 6px rgba(0,0,0,0.8), inset 0 1px 1px rgba(255,255,255,0.2);
}}

.black-key:active, .black-key.active {{
    background: linear-gradient(180deg, #151515 0%, #000000 100%);
    box-shadow: 0 1px 2px rgba(0,0,0,0.9);
}}

.key-label {{
    position: absolute;
    bottom: 6px;
    left: 50%;
    transform: translateX(-50%);
    font-size: 9px;
    font-weight: 700;
    color: #888;
    pointer-events: none;
}}

.black-key .key-label {{
    bottom: 4px;
    color: #666;
}}

/* Drag & Drop Overlay */
.drop-overlay {{
    display: none;
    position: fixed;
    top: 0; left: 0; right: 0; bottom: 0;
    background: rgba(14, 16, 22, 0.9);
    border: 4px dashed var(--accent-blue);
    z-index: 999;
    align-items: center;
    justify-content: center;
    flex-direction: column;
    gap: 16px;
}}

.drop-overlay.visible {{
    display: flex;
}}

.drop-overlay h2 {{
    font-size: 28px;
    font-weight: 800;
    color: white;
}}

/* Toast Notifications */
#toast-container {{
    position: fixed;
    bottom: 140px;
    right: 20px;
    display: flex;
    flex-direction: column;
    gap: 8px;
    z-index: 1000;
}}

.toast {{
    background: #1e212a;
    color: white;
    padding: 10px 16px;
    border-radius: 4px;
    border-left: 4px solid var(--accent-blue);
    box-shadow: 0 4px 14px rgba(0,0,0,0.6);
    font-size: 13px;
    animation: fadeIn 0.2s ease;
}}

.toast.success {{
    border-left-color: var(--accent-green);
}}

.toast.error {{
    border-left-color: var(--accent-red);
}}

@keyframes fadeIn {{
    from {{ opacity: 0; transform: translateY(10px); }}
    to {{ opacity: 1; transform: translateY(0); }}
}}
</style>
</head>
<body>

<!-- Header -->
<header class="synth-header">
    <div class="brand-section">
        <div class="brand-badge">FM-106</div>
        <div>
            <div class="model-title">SYNTHESIZER LIBRARIAN</div>
            <div class="model-subtitle">System Exclusive / Web MIDI Editor</div>
        </div>
    </div>

    <!-- MIDI Connection Bar -->
    <div class="midi-toolbar">
        <span class="led-indicator" id="midi-conn-led" title="MIDI Connection Status"></span>
        <select class="synth-select" id="midi-in-select">
            <option value="">MIDI In: Scanning...</option>
        </select>
        <select class="synth-select" id="midi-out-select">
            <option value="">MIDI Out: Scanning...</option>
        </select>
        <select class="synth-select" id="midi-ch-select" title="MIDI Channel">
            <option value="1">Ch 1</option>
            <option value="2">Ch 2</option>
            <option value="3">Ch 3</option>
            <option value="4">Ch 4</option>
            <option value="5">Ch 5</option>
            <option value="6">Ch 6</option>
            <option value="7">Ch 7</option>
            <option value="8">Ch 8</option>
            <option value="9">Ch 9</option>
            <option value="10">Ch 10</option>
            <option value="11">Ch 11</option>
            <option value="12">Ch 12</option>
            <option value="13">Ch 13</option>
            <option value="14">Ch 14</option>
            <option value="15">Ch 15</option>
            <option value="16">Ch 16</option>
        </select>
        <button class="synth-btn accent" id="btn-midi-connect">Connect</button>
        <div style="display:flex; align-items:center; gap:6px; margin-left:6px;">
            <span style="font-size:10px; color:var(--text-muted); font-weight:700;">TX</span>
            <span class="led-indicator amber" id="led-tx"></span>
            <span style="font-size:10px; color:var(--text-muted); font-weight:700;">RX</span>
            <span class="led-indicator amber" id="led-rx"></span>
        </div>
    </div>
</header>

<!-- Main Workspace -->
<div class="workspace">
    <!-- Left Sidebar: Patch Library Manager -->
    <aside class="library-sidebar">
        <div class="sidebar-header">
            <div class="bank-tabs">
                <div class="bank-tab active" data-bank="FA" id="tab-bank-fa">FACT A</div>
                <div class="bank-tab" data-bank="FB" id="tab-bank-fb">FACT B</div>
                <div class="bank-tab user-tab" data-bank="UA" id="tab-bank-ua">USER A</div>
                <div class="bank-tab user-tab" data-bank="UB" id="tab-bank-ub">USER B</div>
            </div>
            <div class="search-bar">
                <input type="text" class="synth-input" id="search-input" placeholder="Search patches...">
            </div>
        </div>

        <div class="library-actions-bar">
            <button class="synth-btn" id="btn-import-file" title="Import .syx or .json file">Import File</button>
            <button class="synth-btn" id="btn-export-bank" title="Export current bank as .syx">Export .SYX</button>
            <button class="synth-btn" id="btn-export-json" title="Backup full library as JSON">Backup JSON</button>
            <button class="synth-btn amber" id="btn-restore-factory" title="Restore factory presets">Factory Reset</button>
        </div>

        <div class="preset-list-container" id="preset-list">
            <!-- Dynamically populated 64 presets -->
        </div>

        <div class="hardware-sync-bar">
            <div style="display:grid; grid-template-columns: 1fr 1fr; gap:6px;">
                <button class="synth-btn primary" id="btn-save-slot" title="Write current sound to hardware slot">Write Slot</button>
                <button class="synth-btn amber" id="btn-burn-flash" title="Commit user presets to FM-1 SPI Flash!">Burn Flash</button>
            </div>
            <div style="display:grid; grid-template-columns: 1fr 1fr; gap:6px;">
                <button class="synth-btn" id="btn-sync-all-to-hw" title="Send all 128 user presets to FM-1">Send User 128</button>
                <button class="synth-btn" id="btn-read-all-from-hw" title="Pull all 128 user presets from FM-1">Read User 128</button>
            </div>
            <div class="sync-status-text" id="sync-status">Ready. Hardware: Disconnected</div>
        </div>
    </aside>

    <!-- Right Synth Panel: FM-106 Front Panel -->
    <main class="synth-panel">
        <!-- Active Patch Ribbon -->
        <div class="patch-banner">
            <div style="display:flex; align-items:center; gap:10px;">
                <div class="patch-slot-large" id="current-slot-badge">A11</div>
                <div class="patch-bank-badge" id="current-bank-badge" style="font-size:11px; font-weight:800; padding:4px 8px; border-radius:3px; background:#0e2439; color:var(--accent-blue);">[FACT A]</div>
                <div class="patch-name-editor">
                    <input type="text" class="patch-name-input" id="current-patch-name" maxlength="15" value="A11 Brass">
                </div>
            </div>
            <div class="patch-actions">
                <button class="synth-btn primary" id="btn-audition" title="Transmit patch to live manual buffer">Audition</button>
                <button class="synth-btn amber" id="btn-copy-to-user" title="Copy patch to User Bank">Copy to User</button>
                <button class="synth-btn" id="btn-export-single" title="Export this single patch as .syx">Export .SYX</button>
                <button class="synth-btn" id="btn-init-patch" title="Initialize patch to basic saw">Init</button>
            </div>
        </div>

        <!-- Synth Modules Front Panel -->
        <div class="synth-sections">

            <!-- LFO -->
            <div class="synth-module">
                <div class="module-title">LFO</div>
                <div class="module-controls">
                    <div class="fader-control" data-ctrl="0">
                        <div class="fader-val" id="val-lfo-rate">20</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-lfo-rate"></div>
                            <input type="range" class="fader-input" id="fader-lfo-rate" min="0" max="127" value="20">
                        </div>
                        <div class="fader-label">RATE</div>
                    </div>
                    <div class="fader-control" data-ctrl="1">
                        <div class="fader-val" id="val-lfo-delay">49</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-lfo-delay"></div>
                            <input type="range" class="fader-input" id="fader-lfo-delay" min="0" max="127" value="49">
                        </div>
                        <div class="fader-label">DELAY</div>
                    </div>
                </div>
            </div>

            <!-- DCO -->
            <div class="synth-module red-top" style="grid-column: span 2;">
                <div class="module-title">DCO (DIGITALLY CONTROLLED OSCILLATOR)</div>
                <div class="module-controls">
                    <!-- Range / Octave Switch -->
                    <div class="switch-control">
                        <div class="switch-options" id="sw-octave">
                            <div class="switch-opt" data-oct="0">16'</div>
                            <div class="switch-opt active" data-oct="1">8'</div>
                            <div class="switch-opt" data-oct="2">4'</div>
                        </div>
                        <div class="fader-label">RANGE</div>
                    </div>

                    <div class="fader-control" data-ctrl="2">
                        <div class="fader-val" id="val-dco-lfo">0</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-dco-lfo"></div>
                            <input type="range" class="fader-input" id="fader-dco-lfo" min="0" max="127" value="0">
                        </div>
                        <div class="fader-label">LFO</div>
                    </div>

                    <div class="fader-control" data-ctrl="3">
                        <div class="fader-val" id="val-dco-pwm">102</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-dco-pwm"></div>
                            <input type="range" class="fader-input" id="fader-dco-pwm" min="0" max="127" value="102">
                        </div>
                        <div class="fader-label">PWM</div>
                    </div>

                    <!-- PWM Source Switch -->
                    <div class="switch-control">
                        <div class="switch-options" id="sw-pwm-mode">
                            <div class="switch-opt active" data-val="0">MAN</div>
                            <div class="switch-opt" data-val="1">LFO</div>
                        </div>
                        <div class="fader-label">PWM SRC</div>
                    </div>

                    <!-- Pulse Wave Switch -->
                    <div class="switch-control">
                        <div class="switch-options" id="sw-pulse">
                            <div class="switch-opt" data-val="1">ON</div>
                            <div class="switch-opt active" data-val="0">OFF</div>
                        </div>
                        <div class="fader-label">PULSE</div>
                    </div>

                    <!-- Saw Wave Switch -->
                    <div class="switch-control">
                        <div class="switch-options" id="sw-saw">
                            <div class="switch-opt active" data-val="1">ON</div>
                            <div class="switch-opt" data-val="0">OFF</div>
                        </div>
                        <div class="fader-label">SAW</div>
                    </div>

                    <!-- Sub Level Slider -->
                    <div class="fader-control" data-ctrl="15">
                        <div class="fader-val" id="val-dco-sub">0</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-dco-sub"></div>
                            <input type="range" class="fader-input" id="fader-dco-sub" min="0" max="127" value="0">
                        </div>
                        <div class="fader-label">SUB</div>
                    </div>

                    <!-- Noise Level Slider -->
                    <div class="fader-control" data-ctrl="4">
                        <div class="fader-val" id="val-dco-noise">0</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-dco-noise"></div>
                            <input type="range" class="fader-input" id="fader-dco-noise" min="0" max="127" value="0">
                        </div>
                        <div class="fader-label">NOISE</div>
                    </div>
                </div>
            </div>

            <!-- HPF -->
            <div class="synth-module">
                <div class="module-title">HPF</div>
                <div class="module-controls">
                    <div class="switch-control" style="width:70px;">
                        <div class="switch-options" id="sw-hpf">
                            <div class="switch-opt" data-hpf="0">0 (Boost)</div>
                            <div class="switch-opt active" data-hpf="1">1 (Flat)</div>
                            <div class="switch-opt" data-hpf="2">2 (236Hz)</div>
                            <div class="switch-opt" data-hpf="3">3 (754Hz)</div>
                        </div>
                        <div class="fader-label">FREQ</div>
                    </div>
                </div>
            </div>

            <!-- VCF -->
            <div class="synth-module red-top" style="grid-column: span 2;">
                <div class="module-title">VCF (VOLTAGE CONTROLLED FILTER)</div>
                <div class="module-controls">
                    <div class="fader-control" data-ctrl="5">
                        <div class="fader-val" id="val-vcf-cutoff">35</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-vcf-cutoff"></div>
                            <input type="range" class="fader-input" id="fader-vcf-cutoff" min="0" max="127" value="35">
                        </div>
                        <div class="fader-label">FREQ</div>
                    </div>

                    <div class="fader-control" data-ctrl="6">
                        <div class="fader-val" id="val-vcf-res">13</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-vcf-res"></div>
                            <input type="range" class="fader-input" id="fader-vcf-res" min="0" max="127" value="13">
                        </div>
                        <div class="fader-label">RES</div>
                    </div>

                    <!-- ENV Polarity Switch -->
                    <div class="switch-control">
                        <div class="switch-options" id="sw-vcf-polarity">
                            <div class="switch-opt active" data-val="0">NORM</div>
                            <div class="switch-opt" data-val="1">INV</div>
                        </div>
                        <div class="fader-label">POLARITY</div>
                    </div>

                    <div class="fader-control" data-ctrl="7">
                        <div class="fader-val" id="val-vcf-env">58</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-vcf-env"></div>
                            <input type="range" class="fader-input" id="fader-vcf-env" min="0" max="127" value="58">
                        </div>
                        <div class="fader-label">ENV</div>
                    </div>

                    <div class="fader-control" data-ctrl="8">
                        <div class="fader-val" id="val-vcf-lfo">0</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-vcf-lfo"></div>
                            <input type="range" class="fader-input" id="fader-vcf-lfo" min="0" max="127" value="0">
                        </div>
                        <div class="fader-label">LFO</div>
                    </div>

                    <div class="fader-control" data-ctrl="9">
                        <div class="fader-val" id="val-vcf-kybd">86</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-vcf-kybd"></div>
                            <input type="range" class="fader-input" id="fader-vcf-kybd" min="0" max="127" value="86">
                        </div>
                        <div class="fader-label">KYBD</div>
                    </div>
                </div>
            </div>

            <!-- VCA -->
            <div class="synth-module">
                <div class="module-title">VCA</div>
                <div class="module-controls">
                    <!-- VCA Mode Switch -->
                    <div class="switch-control">
                        <div class="switch-options" id="sw-vca-mode">
                            <div class="switch-opt active" data-val="0">ENV</div>
                            <div class="switch-opt" data-val="1">GATE</div>
                        </div>
                        <div class="fader-label">MODE</div>
                    </div>

                    <div class="fader-control" data-ctrl="10">
                        <div class="fader-val" id="val-vca-level">108</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-vca-level"></div>
                            <input type="range" class="fader-input" id="fader-vca-level" min="0" max="127" value="108">
                        </div>
                        <div class="fader-label">LEVEL</div>
                    </div>
                </div>
            </div>

            <!-- ENV -->
            <div class="synth-module amber-top" style="grid-column: span 2;">
                <div class="module-title">ENV (ENVELOPE GENERATOR)</div>
                <div class="module-controls">
                    <div class="fader-control" data-ctrl="11">
                        <div class="fader-val" id="val-env-a">3</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-env-a"></div>
                            <input type="range" class="fader-input" id="fader-env-a" min="0" max="127" value="3">
                        </div>
                        <div class="fader-label">A</div>
                    </div>

                    <div class="fader-control" data-ctrl="12">
                        <div class="fader-val" id="val-env-d">49</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-env-d"></div>
                            <input type="range" class="fader-input" id="fader-env-d" min="0" max="127" value="49">
                        </div>
                        <div class="fader-label">D</div>
                    </div>

                    <div class="fader-control" data-ctrl="13">
                        <div class="fader-val" id="val-env-s">45</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-env-s"></div>
                            <input type="range" class="fader-input" id="fader-env-s" min="0" max="127" value="45">
                        </div>
                        <div class="fader-label">S</div>
                    </div>

                    <div class="fader-control" data-ctrl="14">
                        <div class="fader-val" id="val-env-r">32</div>
                        <div class="fader-track-wrap">
                            <div class="fader-track-ticks"></div>
                            <div class="fader-slot"></div>
                            <div class="fader-cap" id="cap-env-r"></div>
                            <input type="range" class="fader-input" id="fader-env-r" min="0" max="127" value="32">
                        </div>
                        <div class="fader-label">R</div>
                    </div>
                </div>
            </div>

            <!-- CHORUS -->
            <div class="synth-module amber-top">
                <div class="module-title">CHORUS</div>
                <div class="module-controls" style="align-items:center;">
                    <div class="chorus-buttons">
                        <button class="chorus-btn" data-chorus="0" id="btn-chorus-off">
                            <span>OFF</span>
                            <span class="led-indicator amber"></span>
                        </button>
                        <button class="chorus-btn active" data-chorus="1" id="btn-chorus-i">
                            <span>CHORUS I</span>
                            <span class="led-indicator amber active"></span>
                        </button>
                        <button class="chorus-btn" data-chorus="2" id="btn-chorus-ii">
                            <span>CHORUS II</span>
                            <span class="led-indicator amber"></span>
                        </button>
                    </div>
                </div>
            </div>

        </div>
    </main>
</div>

<!-- Virtual Audition Keyboard -->
<footer class="keyboard-footer">
    <div class="keyboard-controls">
        <div class="octave-controls">
            <span style="font-weight:700; color:var(--text-muted);">OCTAVE</span>
            <button class="synth-btn" id="btn-oct-down">-</button>
            <span id="label-octave" style="font-family:var(--font-mono); font-weight:700; color:var(--accent-amber);">C3 - C5</span>
            <button class="synth-btn" id="btn-oct-up">+</button>
        </div>
        <div style="color:var(--text-muted); font-size:11px;">
            Audition synth with Mouse, Touch, or QWERTY keys: <b>[A W S E D F T G Y H U J K]</b>
        </div>
        <div style="display:flex; align-items:center; gap:8px;">
            <span style="font-weight:700; color:var(--text-muted);">VELOCITY</span>
            <input type="range" id="input-velocity" min="1" max="127" value="100" style="width:80px; accent-color:var(--accent-red);">
            <span id="val-velocity" style="font-family:var(--font-mono); color:var(--accent-amber); font-size:11px;">100</span>
        </div>
    </div>

    <div class="keyboard-keys" id="piano-keyboard">
        <!-- 25 keys populated dynamically -->
    </div>
</footer>

<!-- Drag & Drop Overlay -->
<div class="drop-overlay" id="drop-overlay">
    <h2>Drop .SYX, .106, or .JSON preset file to import</h2>
    <p style="color:var(--text-muted);">Supports System Exclusive (.syx), Jarvik7 .106 libraries, and web editor files</p>
</div>

<!-- Hidden File Input -->
<input type="file" id="file-picker" accept=".syx,.106,.json,.mid" style="display:none;">

<!-- Toast notifications container -->
<div id="toast-container"></div>

<script>
// 128 Compact Factory Presets Embedded
const FACTORY_PRESETS = {presets_js_code};

// State: 256 Presets total (0..63 Factory A, 64..127 Factory B, 128..191 User A, 192..255 User B)
let allPresets = [];
for (let i = 0; i < 128; i++) {{
    allPresets.push(JSON.parse(JSON.stringify(FACTORY_PRESETS[i])));
}}
for (let i = 0; i < 128; i++) {{
    const copy = JSON.parse(JSON.stringify(FACTORY_PRESETS[i]));
    const prefix = i < 64 ? 'UA' : 'UB';
    const subIdx = i % 64;
    const grp = Math.floor(subIdx / 8) + 1;
    const ptc = (subIdx % 8) + 1;
    copy.name = copy.name.replace(/^[AB]\d\d\s*/, `${{prefix}}${{grp}}${{ptc}} `);
    allPresets.push(copy);
}}

let currentBank = 'FA'; // 'FA', 'FB', 'UA', 'UB'
let selectedSlot = 0;   // 0 .. 255
let baseOctave = 3;     // C3 base note 48
let midiAccess = null;
let midiInput = null;
let midiOutput = null;
let isHardwareConnected = false;

// Helpers
function indexToSlot(idx) {{
    const bankIdx = Math.floor(idx / 64) % 4;
    const subIdx = idx % 64;
    const group = Math.floor(subIdx / 8) + 1;
    const patch = (subIdx % 8) + 1;
    const prefix = ['A', 'B', 'UA', 'UB'][bankIdx];
    return `${{prefix}}${{group}}${{patch}}`;
}}

function getBankStartIdx(bank) {{
    switch (bank) {{
        case 'FA': return 0;
        case 'FB': return 64;
        case 'UA': return 128;
        case 'UB': return 192;
        default: return 0;
    }}
}}

function getBankFromIdx(idx) {{
    const b = Math.floor(idx / 64) % 4;
    return ['FA', 'FB', 'UA', 'UB'][b];
}}

function isFactorySlot(idx) {{
    return idx < 128;
}}

function showToast(msg, type = 'info') {{
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = `toast ${{type}}`;
    toast.textContent = msg;
    container.appendChild(toast);
    setTimeout(() => {{
        toast.style.opacity = '0';
        toast.style.transform = 'translateY(-10px)';
        toast.style.transition = 'all 0.3s ease';
        setTimeout(() => toast.remove(), 300);
    }}, 3200);
}}

function flashTx() {{
    const led = document.getElementById('led-tx');
    led.classList.add('active');
    clearTimeout(led._timeout);
    led._timeout = setTimeout(() => led.classList.remove('active'), 80);
}}

function flashRx() {{
    const led = document.getElementById('led-rx');
    led.classList.add('active');
    clearTimeout(led._timeout);
    led._timeout = setTimeout(() => led.classList.remove('active'), 80);
}}

function updateSyncStatus(text) {{
    document.getElementById('sync-status').textContent = text;
}}

// Fader Cap Positioning
function updateFaderCap(ctrlId, val) {{
    const sliderIds = [
        'lfo-rate', 'lfo-delay', 'dco-lfo', 'dco-pwm', 'dco-noise',
        'vcf-cutoff', 'vcf-res', 'vcf-env', 'vcf-lfo', 'vcf-kybd',
        'vca-level', 'env-a', 'env-d', 'env-s', 'env-r', 'dco-sub'
    ];
    const id = sliderIds[ctrlId];
    if (!id) return;
    const cap = document.getElementById(`cap-${{id}}`);
    const valEl = document.getElementById(`val-${{id}}`);
    const fader = document.getElementById(`fader-${{id}}`);
    if (fader && val !== undefined) fader.value = val;
    const curVal = fader ? parseInt(fader.value, 10) : val;
    if (valEl) valEl.textContent = curVal;
    if (cap) {{
        // Bottom is 6px, Top is 100px: range is 94px
        const pct = curVal / 127;
        const topPx = 100 - (pct * 94);
        cap.style.top = `${{topPx}}px`;
    }}
}}

// Update UI from active preset object
function loadPresetToUI(preset) {{
    if (!preset) return;
    const slotCode = indexToSlot(selectedSlot);
    const bank = getBankFromIdx(selectedSlot);
    const isFact = isFactorySlot(selectedSlot);
    const badgeEl = document.getElementById('current-bank-badge');
    const slotEl = document.getElementById('current-slot-badge');
    const nameEl = document.getElementById('current-patch-name');

    if (slotEl) slotEl.textContent = slotCode;
    if (nameEl) {{
        nameEl.value = preset.name;
        nameEl.readOnly = isFact;
        nameEl.title = isFact ? "Factory preset (read-only; click 'Copy to User' to edit)" : "User preset name";
    }}
    if (badgeEl) {{
        switch (bank) {{
            case 'FA':
                badgeEl.textContent = '[FACT A]';
                badgeEl.style.color = 'var(--accent-blue)';
                badgeEl.style.background = '#0e2439';
                break;
            case 'FB':
                badgeEl.textContent = '[FACT B]';
                badgeEl.style.color = 'var(--accent-blue)';
                badgeEl.style.background = '#0e2439';
                break;
            case 'UA':
                badgeEl.textContent = '[USER A]';
                badgeEl.style.color = 'var(--accent-amber)';
                badgeEl.style.background = '#362208';
                break;
            case 'UB':
                badgeEl.textContent = '[USER B]';
                badgeEl.style.color = 'var(--accent-amber)';
                badgeEl.style.background = '#362208';
                break;
        }}
    }}

    // Sliders
    for (let c = 0; c < 16; c++) {{
        updateFaderCap(c, preset.sliders[c]);
    }}

    // SW1: Octave (bits 0-2), Pulse (bit 3), Saw (bit 4), Chorus (bits 5-6)
    const sw1 = preset.sw1;
    const oct = (sw1 & 4) ? 2 : ((sw1 & 2) ? 1 : 0);
    setSwitchGroupActive('sw-octave', 'oct', oct);

    const pulse = (sw1 & 8) ? 1 : 0;
    setSwitchGroupActive('sw-pulse', 'val', pulse);

    const saw = (sw1 & 16) ? 1 : 0;
    setSwitchGroupActive('sw-saw', 'val', saw);

    let chorus = 0;
    if (sw1 & 32) chorus = 0;
    else if (sw1 & 64) chorus = 1;
    else chorus = 2;
    setChorusActive(chorus);

    // SW2: PWM Mode (bit 0), VCF Inv (bit 1), VCA Mode (bit 2), HPF (bits 3-4)
    const sw2 = preset.sw2;
    const pwmMode = (sw2 & 1) ? 1 : 0;
    setSwitchGroupActive('sw-pwm-mode', 'val', pwmMode);

    const vcfInv = (sw2 & 2) ? 1 : 0;
    setSwitchGroupActive('sw-vcf-polarity', 'val', vcfInv);

    const vcaMode = (sw2 & 4) ? 1 : 0;
    setSwitchGroupActive('sw-vca-mode', 'val', vcaMode);

    const hpf = (3 - ((sw2 >> 3) & 3)) & 3;
    setSwitchGroupActive('sw-hpf', 'hpf', hpf);
}}

function setSwitchGroupActive(containerId, attr, val) {{
    const el = document.getElementById(containerId);
    if (!el) return;
    el.querySelectorAll('.switch-opt').forEach(opt => {{
        if (parseInt(opt.dataset[attr], 10) === val) {{
            opt.classList.add('active');
        }} else {{
            opt.classList.remove('active');
        }}
    }});
}}

function setChorusActive(mode) {{
    document.querySelectorAll('.chorus-btn').forEach(btn => {{
        const m = parseInt(btn.dataset.chorus, 10);
        const led = btn.querySelector('.led-indicator');
        if (m === mode) {{
            btn.classList.add('active');
            if (led) led.classList.add('active');
        }} else {{
            btn.classList.remove('active');
            if (led) led.classList.remove('active');
        }}
    }});
}}

// Read current UI parameters into preset object
function readUIToPreset() {{
    const name = document.getElementById('current-patch-name').value.trim() || `${{indexToSlot(selectedSlot)}} Patch`;
    const sliderIds = [
        'lfo-rate', 'lfo-delay', 'dco-lfo', 'dco-pwm', 'dco-noise',
        'vcf-cutoff', 'vcf-res', 'vcf-env', 'vcf-lfo', 'vcf-kybd',
        'vca-level', 'env-a', 'env-d', 'env-s', 'env-r', 'dco-sub'
    ];
    const sliders = sliderIds.map(id => parseInt(document.getElementById(`fader-${{id}}`).value, 10));

    // SW1
    const octOpt = document.querySelector('#sw-octave .switch-opt.active');
    const oct = octOpt ? parseInt(octOpt.dataset.oct, 10) : 1;
    let sw1 = oct === 2 ? 4 : (oct === 1 ? 2 : 1);

    const pulseOpt = document.querySelector('#sw-pulse .switch-opt.active');
    if (pulseOpt && parseInt(pulseOpt.dataset.val, 10) === 1) sw1 |= 8;

    const sawOpt = document.querySelector('#sw-saw .switch-opt.active');
    if (sawOpt && parseInt(sawOpt.dataset.val, 10) === 1) sw1 |= 16;

    const chorusBtn = document.querySelector('.chorus-btn.active');
    const chorus = chorusBtn ? parseInt(chorusBtn.dataset.chorus, 10) : 0;
    if (chorus === 0) sw1 |= 32;
    else if (chorus === 1) sw1 |= 64;

    // SW2
    let sw2 = 0;
    const pwmOpt = document.querySelector('#sw-pwm-mode .switch-opt.active');
    if (pwmOpt && parseInt(pwmOpt.dataset.val, 10) === 1) sw2 |= 1;

    const vcfOpt = document.querySelector('#sw-vcf-polarity .switch-opt.active');
    if (vcfOpt && parseInt(vcfOpt.dataset.val, 10) === 1) sw2 |= 2;

    const vcaOpt = document.querySelector('#sw-vca-mode .switch-opt.active');
    if (vcaOpt && parseInt(vcaOpt.dataset.val, 10) === 1) sw2 |= 4;

    const hpfOpt = document.querySelector('#sw-hpf .switch-opt.active');
    const hpf = hpfOpt ? parseInt(hpfOpt.dataset.hpf, 10) : 1;
    sw2 |= (((3 - hpf) & 3) << 3);

    return {{
        name: name,
        sliders: sliders,
        sw1: sw1,
        sw2: sw2
    }};
}}

// Render Preset List for current bank
function renderPresetList(filter = '') {{
    const container = document.getElementById('preset-list');
    container.innerHTML = '';
    const startIdx = getBankStartIdx(currentBank);
    const endIdx = startIdx + 64;
    const isFact = isFactorySlot(startIdx);

    for (let i = startIdx; i < endIdx; i++) {{
        const preset = allPresets[i];
        const slotCode = indexToSlot(i);
        if (filter && !preset.name.toLowerCase().includes(filter.toLowerCase()) && !slotCode.toLowerCase().includes(filter.toLowerCase())) {{
            continue;
        }}

        const item = document.createElement('div');
        item.className = `preset-item ${{i === selectedSlot ? 'selected' : ''}} ${{isFact ? 'factory-item' : 'user-item'}}`;
        item.dataset.idx = i;

        item.innerHTML = `
            <span class="preset-slot-badge" style="background:${{isFact ? '#0e2439' : '#362208'}}; color:${{isFact ? 'var(--accent-blue)' : 'var(--accent-amber)'}};">${{slotCode}}</span>
            <span class="preset-item-name">${{preset.name}}</span>
            <div class="preset-quick-actions">
                <button class="mini-btn play-btn" title="Audition this patch">&#9658;</button>
                <button class="mini-btn copy-btn" title="Copy patch">&#128203;</button>
            </div>
        `;

        item.addEventListener('click', (e) => {{
            if (e.target.closest('.play-btn')) {{
                e.stopPropagation();
                selectSlot(i);
                auditionCurrentPreset();
                return;
            }}
            if (e.target.closest('.copy-btn')) {{
                e.stopPropagation();
                copyPresetToClipboard(i);
                return;
            }}
            selectSlot(i);
        }});

        container.appendChild(item);
    }}
}}

let copiedPreset = null;
function copyPresetToClipboard(idx) {{
    copiedPreset = JSON.parse(JSON.stringify(allPresets[idx]));
    showToast(`Copied "${{copiedPreset.name}}". Select a User slot and press Paste.`, 'info');
}}

function copyCurrentToUser() {{
    const current = readUIToPreset();
    let targetSlot = selectedSlot >= 128 ? selectedSlot : selectedSlot + 128;
    allPresets[targetSlot] = JSON.parse(JSON.stringify(current));
    const subIdx = targetSlot % 64;
    const grp = Math.floor(subIdx / 8) + 1;
    const ptc = (subIdx % 8) + 1;
    const prefix = targetSlot < 192 ? 'UA' : 'UB';
    allPresets[targetSlot].name = `${{prefix}}${{grp}}${{ptc}} ${{current.name.replace(/^[A-Za-z0-9]+\s*/, '')}}`.trim();
    showToast(`Copied "${{current.name}}" into User slot ${{indexToSlot(targetSlot)}}!`, 'success');
    selectSlot(targetSlot);
}}

function selectSlot(idx) {{
    if (idx < 0) idx = 0;
    if (idx >= 256) idx = 255;
    if (selectedSlot >= 128) {{
        allPresets[selectedSlot] = readUIToPreset();
    }}
    selectedSlot = idx;
    currentBank = getBankFromIdx(idx);
    updateBankTabs();
    loadPresetToUI(allPresets[selectedSlot]);
    renderPresetList(document.getElementById('search-input').value);
}}

function updateBankTabs() {{
    document.getElementById('tab-bank-fa').classList.toggle('active', currentBank === 'FA');
    document.getElementById('tab-bank-fb').classList.toggle('active', currentBank === 'FB');
    document.getElementById('tab-bank-ua').classList.toggle('active', currentBank === 'UA');
    document.getElementById('tab-bank-ub').classList.toggle('active', currentBank === 'UB');
}}

// Web MIDI Engine
async function initMIDI() {{
    if (!navigator.requestMIDIAccess) {{
        showToast('Web MIDI is not supported in this browser. Please use Chrome, Edge, or Opera.', 'error');
        updateSyncStatus('Web MIDI not supported');
        return;
    }}
    try {{
        midiAccess = await navigator.requestMIDIAccess({{ sysex: true }});
        updateMIDIPorts();
        midiAccess.onstatechange = () => updateMIDIPorts();
        showToast('MIDI subsystem initialized.', 'success');
    }} catch (err) {{
        showToast('Web MIDI access error: ' + err.message, 'error');
        updateSyncStatus('MIDI access denied: ' + err.message);
    }}
}}

function updateMIDIPorts() {{
    if (!midiAccess) return;
    const inSelect = document.getElementById('midi-in-select');
    const outSelect = document.getElementById('midi-out-select');

    const prevIn = inSelect.value;
    const prevOut = outSelect.value;

    inSelect.innerHTML = '<option value="">-- Select MIDI In --</option>';
    outSelect.innerHTML = '<option value="">-- Select MIDI Out --</option>';

    let autoInId = null;
    let autoOutId = null;

    for (const input of midiAccess.inputs.values()) {{
        const opt = document.createElement('option');
        opt.value = input.id;
        opt.textContent = `In: ${{input.name}}`;
        inSelect.appendChild(opt);
        if (input.name.toLowerCase().includes('felucca') || input.name.toLowerCase().includes('fm-1')) {{
            autoInId = input.id;
        }}
    }}

    for (const output of midiAccess.outputs.values()) {{
        const opt = document.createElement('option');
        opt.value = output.id;
        opt.textContent = `Out: ${{output.name}}`;
        outSelect.appendChild(opt);
        if (output.name.toLowerCase().includes('felucca') || output.name.toLowerCase().includes('fm-1')) {{
            autoOutId = output.id;
        }}
    }}

    if (autoInId) inSelect.value = autoInId;
    else if (prevIn) inSelect.value = prevIn;

    if (autoOutId) outSelect.value = autoOutId;
    else if (prevOut) outSelect.value = prevOut;

    if (autoInId && autoOutId && !isHardwareConnected) {{
        connectMIDI();
    }}
}}

function connectMIDI() {{
    const inId = document.getElementById('midi-in-select').value;
    const outId = document.getElementById('midi-out-select').value;
    const connLed = document.getElementById('midi-conn-led');

    if (!inId || !outId) {{
        showToast('Please select both MIDI In and MIDI Out ports.', 'error');
        return;
    }}

    if (midiInput) midiInput.onmidimessage = null;

    midiInput = midiAccess.inputs.get(inId);
    midiOutput = midiAccess.outputs.get(outId);

    if (midiInput && midiOutput) {{
        midiInput.onmidimessage = handleMIDIMessage;
        isHardwareConnected = true;
        connLed.classList.add('active');
        document.getElementById('btn-midi-connect').textContent = 'Connected';
        document.getElementById('btn-midi-connect').classList.remove('accent');
        document.getElementById('btn-midi-connect').classList.add('primary');
        updateSyncStatus(`Connected to ${{midiOutput.name}}`);
        showToast(`Connected to ${{midiOutput.name}}`, 'success');
    }}
}}

function getMidiChannel() {{
    return parseInt(document.getElementById('midi-ch-select').value, 10) - 1;
}}

function encodePatchName(name) {{
    const bytes = [];
    const clean = (name || '').padEnd(16, ' ').slice(0, 16);
    for (let i = 0; i < 16; i++) {{
        bytes.push(clean.charCodeAt(i) & 0x7F);
    }}
    return bytes;
}}

// Handle incoming SysEx from FM-106
function handleMIDIMessage(event) {{
    const data = event.data;
    if (data[0] !== 0xF0) return;
    flashRx();

    if (data[1] === 0x41) {{
        const cmd = data[2];
        const ch = data[3];

        // 0x30: APR Patch Dump Response
        if (cmd === 0x30 && data.length >= 24) {{
            const bankOpt = (data[3] >> 4) & 0x07;
            let slotIdx;
            if (bankOpt >= 1 && bankOpt <= 4) {{
                slotIdx = (bankOpt - 1) * 64 + (data[4] % 64);
            }} else {{
                const bIdx = getBankStartIdx(currentBank);
                slotIdx = bIdx + (data[4] % 64);
            }}

            const sliders = Array.from(data.slice(5, 21));
            const sw1 = data[21];
            const sw2 = data[22];
            let name = '';
            if (data.length >= 40) {{
                for (let i = 23; i < 39; i++) {{
                    if (data[i] >= 32 && data[i] < 127) name += String.fromCharCode(data[i]);
                }}
                name = name.trim();
            }}
            if (!name) name = `${{indexToSlot(slotIdx)}} Patch`;

            allPresets[slotIdx] = {{
                name: name,
                sliders: sliders,
                sw1: sw1,
                sw2: sw2
            }};

            if (selectedSlot === slotIdx) {{
                loadPresetToUI(allPresets[slotIdx]);
            }}
            renderPresetList(document.getElementById('search-input').value);
            updateSyncStatus(`Received ${{indexToSlot(slotIdx)}}: ${{name}}`);
        }}
        // 0x33: Patch Name Response (extended)
        else if (cmd === 0x33 && data.length >= 7) {{
            const bankIdx = data[5] & 0x03;
            const slotIdx = bankIdx * 64 + (data[4] % 64);
            let name = '';
            for (let i = 6; i < data.length - 1; i++) {{
                if (data[i] >= 32 && data[i] < 127) name += String.fromCharCode(data[i]);
            }}
            name = name.trim();
            if (name && allPresets[slotIdx]) {{
                allPresets[slotIdx].name = name;
                if (selectedSlot === slotIdx) {{
                    document.getElementById('current-patch-name').value = name;
                }}
                renderPresetList(document.getElementById('search-input').value);
            }}
        }}
        // 0x35: Flash Commit ACK
        else if (cmd === 0x35 && data[4] === 0x00) {{
            showToast('User presets successfully committed to FM-106 SPI Flash!', 'success');
            updateSyncStatus('Flash save committed successfully.');
        }}
    }}
}}

// Transmit Real-Time IPR SysEx
let iprThrottleTimeout = null;
function sendIPR(ctrl, val) {{
    if (!midiOutput) return;
    const ch = getMidiChannel();
    // F0 41 32 0n ctrl val F7
    const msg = [0xF0, 0x41, 0x32, ch & 0x0F, ctrl, val & 0x7F, 0xF7];
    midiOutput.send(msg);
    flashTx();
}}

// Audition Preset to Manual Edit Buffer (SysEx 0x31)
function auditionCurrentPreset() {{
    if (selectedSlot >= 128) {{
        allPresets[selectedSlot] = readUIToPreset();
    }}
    const p = allPresets[selectedSlot];
    if (!midiOutput) {{
        showToast(`Auditioning "${{p.name}}" locally (Connect MIDI for hardware play)`, 'info');
        return;
    }}
    const ch = getMidiChannel();
    const nameBytes = encodePatchName(p.name);
    // F0 41 31 0n 00 [16 sliders] sw1 sw2 [16 name bytes] F7
    const msg = [
        0xF0, 0x41, 0x31, ch & 0x0F, 0x00,
        ...p.sliders,
        p.sw1,
        p.sw2,
        ...nameBytes,
        0xF7
    ];
    midiOutput.send(msg);
    flashTx();
    showToast(`Auditioning "${{p.name}}" on FM-106`, 'success');
    updateSyncStatus(`Auditioning ${{p.name}}`);
}}

// Store Preset to Slot on Hardware (SysEx 0x30)
function saveCurrentSlotToHardware() {{
    if (isFactorySlot(selectedSlot)) {{
        showToast('Factory Presets (ROM) are protected. Use "Copy to User" to save edits to User Banks.', 'warning');
        return;
    }}
    allPresets[selectedSlot] = readUIToPreset();
    const p = allPresets[selectedSlot];
    if (!midiOutput) {{
        showToast('MIDI not connected. Saved in browser library.', 'info');
        renderPresetList();
        return;
    }}
    const ch = getMidiChannel();
    const bankOpt = selectedSlot < 192 ? 3 : 4; // 3 = User A, 4 = User B
    const localSlot = selectedSlot % 64;
    const chByte = ((bankOpt << 4) & 0x70) | (ch & 0x0F);
    const nameBytes = encodePatchName(p.name);
    // F0 41 30 chByte localSlot [16 sliders] sw1 sw2 [16 name bytes] F7
    const msg = [
        0xF0, 0x41, 0x30, chByte, localSlot & 0x7F,
        ...p.sliders,
        p.sw1,
        p.sw2,
        ...nameBytes,
        0xF7
    ];
    midiOutput.send(msg);
    flashTx();
    showToast(`Stored "${{p.name}}" to hardware slot ${{indexToSlot(selectedSlot)}}`, 'success');
    updateSyncStatus(`Stored ${{indexToSlot(selectedSlot)}} to hardware`);
    renderPresetList();
}}

// Commit All User Presets to SPI Flash (SysEx 0x35)
function burnToHardwareFlash() {{
    if (!midiOutput) {{
        showToast('MIDI not connected. Connect FM-106 first.', 'error');
        return;
    }}
    if (!confirm('Commit all 128 User presets (User A & B) to FM-106 hardware SPI Flash? These will survive power off.')) return;
    const ch = getMidiChannel();
    // F0 41 35 0n 01 F7
    const msg = [0xF0, 0x41, 0x35, ch & 0x0F, 0x01, 0xF7];
    midiOutput.send(msg);
    flashTx();
    updateSyncStatus('Burning user presets to SPI Flash...');
}}

// Dump single slot request (SysEx 0x34)
function requestSlotFromHardware(slotIdx) {{
    if (!midiOutput) return;
    const ch = getMidiChannel();
    const bankIdx = Math.floor(slotIdx / 64) % 4;
    const bankOpt = bankIdx + 1; // 1=Fact A, 2=Fact B, 3=User A, 4=User B
    const localSlot = slotIdx % 64;
    const chByte = ((bankOpt << 4) & 0x70) | (ch & 0x0F);
    // F0 41 34 chByte localSlot F7
    const msg = [0xF0, 0x41, 0x34, chByte, localSlot & 0x7F, 0xF7];
    midiOutput.send(msg);
    flashTx();
}}

// Bulk Sync 128 User Presets to Hardware
async function sendAllToHardware() {{
    if (!midiOutput) {{
        showToast('MIDI not connected.', 'error');
        return;
    }}
    if (!confirm('Send all 128 User presets (User A & B) to FM-106 hardware RAM? Factory presets in ROM will remain safe.')) return;
    if (selectedSlot >= 128) {{
        allPresets[selectedSlot] = readUIToPreset();
    }}
    const ch = getMidiChannel();

    for (let i = 128; i < 256; i++) {{
        const p = allPresets[i];
        const bankOpt = i < 192 ? 3 : 4;
        const localSlot = i % 64;
        const chByte = ((bankOpt << 4) & 0x70) | (ch & 0x0F);
        const nameBytes = encodePatchName(p.name);
        const msg = [
            0xF0, 0x41, 0x30, chByte, localSlot & 0x7F,
            ...p.sliders,
            p.sw1,
            p.sw2,
            ...nameBytes,
            0xF7
        ];
        midiOutput.send(msg);
        flashTx();
        updateSyncStatus(`Sending preset ${{i - 127}}/128 (${{indexToSlot(i)}})...`);
        await new Promise(r => setTimeout(r, 25));
    }}
    showToast('All 128 User presets sent to hardware RAM!', 'success');
    updateSyncStatus('User presets transferred to hardware.');
}}

// Bulk Read All 128 User Presets from Hardware
async function readAllFromHardware() {{
    if (!midiOutput) {{
        showToast('MIDI not connected.', 'error');
        return;
    }}
    if (!confirm('Dump all 128 User presets (User A & B) from FM-106 hardware into library?')) return;

    for (let i = 128; i < 256; i++) {{
        requestSlotFromHardware(i);
        updateSyncStatus(`Requesting user preset ${{i - 127}}/128 (${{indexToSlot(i)}})...`);
        await new Promise(r => setTimeout(r, 40));
    }}
    showToast('User presets dump complete.', 'success');
}}

// Playable Virtual Keyboard
function initKeyboard() {{
    const container = document.getElementById('piano-keyboard');
    container.innerHTML = '';

    const whiteNotes = [0, 2, 4, 5, 7, 9, 11]; // C D E F G A B
    const blackNotes = [1, 3, 6, 8, 10];      // C# D# F# G# A#
    const totalKeys = 25; // 2 octaves + 1 note (C3 to C5)

    const baseMidi = baseOctave * 12; // C3 = 48
    let whiteIndex = 0;

    for (let i = 0; i < totalKeys; i++) {{
        const noteInOct = i % 12;
        const midiNote = baseMidi + i;

        if (whiteNotes.includes(noteInOct)) {{
            const key = document.createElement('div');
            key.className = 'key white-key';
            key.dataset.note = midiNote;
            key.dataset.whiteIdx = whiteIndex++;

            const lbl = document.createElement('div');
            lbl.className = 'key-label';
            const noteNames = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
            const octNum = Math.floor(midiNote / 12) - 1;
            lbl.textContent = noteInOct === 0 ? `C${{octNum}}` : '';
            key.appendChild(lbl);

            container.appendChild(key);
        }}
    }}

    // Place black keys absolutely over white keys
    const blackKeyPositions = [
        {{ note: 1,  whiteIdx: 0 }}, // C#
        {{ note: 3,  whiteIdx: 1 }}, // D#
        {{ note: 6,  whiteIdx: 3 }}, // F#
        {{ note: 8,  whiteIdx: 4 }}, // G#
        {{ note: 10, whiteIdx: 5 }}, // A#
        {{ note: 13, whiteIdx: 7 }}, // C#
        {{ note: 15, whiteIdx: 8 }}, // D#
        {{ note: 18, whiteIdx: 10 }},// F#
        {{ note: 20, whiteIdx: 11 }},// G#
        {{ note: 22, whiteIdx: 12 }},// A#
    ];

    const whiteKeyEls = container.querySelectorAll('.white-key');
    const totalWhites = whiteKeyEls.length;

    blackKeyPositions.forEach(b => {{
        const midiNote = baseMidi + b.note;
        const key = document.createElement('div');
        key.className = 'key black-key';
        key.dataset.note = midiNote;

        // Position relative to total white width percentage
        const leftPct = ((b.whiteIdx + 0.65) / totalWhites) * 100;
        key.style.left = `${{leftPct}}%`;

        container.appendChild(key);
    }});

    // Keyboard events
    const allKeys = container.querySelectorAll('.key');
    allKeys.forEach(k => {{
        const note = parseInt(k.dataset.note, 10);
        k.addEventListener('mousedown', (e) => {{
            e.preventDefault();
            playNoteOn(note);
        }});
        k.addEventListener('mouseenter', (e) => {{
            if (e.buttons === 1) playNoteOn(note);
        }});
        k.addEventListener('mouseleave', () => playNoteOff(note));
        k.addEventListener('mouseup', () => playNoteOff(note));

        // Touch support
        k.addEventListener('touchstart', (e) => {{
            e.preventDefault();
            playNoteOn(note);
        }});
        k.addEventListener('touchend', (e) => {{
            e.preventDefault();
            playNoteOff(note);
        }});
    }});
}}

function playNoteOn(note) {{
    const keyEl = document.querySelector(`.key[data-note="${{note}}"]`);
    if (keyEl) keyEl.classList.add('active');

    if (midiOutput) {{
        const ch = getMidiChannel();
        const vel = parseInt(document.getElementById('input-velocity').value, 10);
        midiOutput.send([0x90 | (ch & 0x0F), note, vel & 0x7F]);
        flashTx();
    }}
}}

function playNoteOff(note) {{
    const keyEl = document.querySelector(`.key[data-note="${{note}}"]`);
    if (keyEl) keyEl.classList.remove('active');

    if (midiOutput) {{
        const ch = getMidiChannel();
        midiOutput.send([0x80 | (ch & 0x0F), note, 0]);
        flashTx();
    }}
}}

// Computer QWERTY keyboard map
const QWERTY_MAP = {{
    'KeyA': 0,  // C
    'KeyW': 1,  // C#
    'KeyS': 2,  // D
    'KeyE': 3,  // D#
    'KeyD': 4,  // E
    'KeyF': 5,  // F
    'KeyT': 6,  // F#
    'KeyG': 7,  // G
    'KeyY': 8,  // G#
    'KeyH': 9,  // A
    'KeyU': 10, // A#
    'KeyJ': 11, // B
    'KeyK': 12, // C
    'KeyO': 13, // C#
    'KeyL': 14, // D
    'KeyP': 15, // D#
    'Semicolon': 16 // E
}};
const activeKeys = new Set();

window.addEventListener('keydown', (e) => {{
    if (e.target.tagName === 'INPUT') return;
    if (e.repeat) return;
    const offset = QWERTY_MAP[e.code];
    if (offset !== undefined) {{
        const note = (baseOctave * 12) + offset;
        activeKeys.add(e.code);
        playNoteOn(note);
    }}
}});

window.addEventListener('keyup', (e) => {{
    if (e.target.tagName === 'INPUT') return;
    const offset = QWERTY_MAP[e.code];
    if (offset !== undefined) {{
        const note = (baseOctave * 12) + offset;
        activeKeys.delete(e.code);
        playNoteOff(note);
    }}
}});

// Octave transpose buttons
document.getElementById('btn-oct-down').addEventListener('click', () => {{
    if (baseOctave > 1) {{
        baseOctave--;
        updateOctaveLabel();
        initKeyboard();
    }}
}});

document.getElementById('btn-oct-up').addEventListener('click', () => {{
    if (baseOctave < 6) {{
        baseOctave++;
        updateOctaveLabel();
        initKeyboard();
    }}
}});

function updateOctaveLabel() {{
    document.getElementById('label-octave').textContent = `C${{baseOctave}} - C${{baseOctave + 2}}`;
}}

document.getElementById('input-velocity').addEventListener('input', (e) => {{
    document.getElementById('val-velocity').textContent = e.target.value;
}});

// Import & Export SysEx
function parseSyxBuffer(buf) {{
    const bytes = new Uint8Array(buf);
    const parsedPatches = [];

    let i = 0;
    while (i < bytes.length) {{
        if (bytes[i] === 0xF0 && bytes[i+1] === 0x41 && (bytes[i+2] === 0x30 || bytes[i+2] === 0x31)) {{
            // Find F7
            let end = i;
            while (end < bytes.length && bytes[end] !== 0xF7) end++;
            if (end < bytes.length && bytes[end] === 0xF7) {{
                const msgLen = end - i + 1;
                if (msgLen >= 24) {{
                    const slot = bytes[i + 4] % 128;
                    const sliders = Array.from(bytes.slice(i + 5, i + 21));
                    const sw1 = bytes[i + 21];
                    const sw2 = bytes[i + 22];
                    let name = '';
                    if (msgLen >= 40) {{
                        for (let k = 23; k < 39; k++) {{
                            const b = bytes[i + k];
                            if (b >= 32 && b < 127) name += String.fromCharCode(b);
                        }}
                        name = name.trim();
                    }}
                    if (!name) name = `${{indexToSlot(slot)}} Imported`;
                    parsedPatches.push({{
                        slot: slot,
                        preset: {{
                            name: name,
                            sliders: sliders,
                            sw1: sw1,
                            sw2: sw2
                        }}
                    }});
                }}
                i = end + 1;
                continue;
            }}
        }}
        i++;
    }}
    return parsedPatches;
}}

function parseJ106Buffer(buf) {{
    try {{
        const view = new DataView(buf);
        let pos = 0;
        if (buf.byteLength < 30) return null;
        let magic = "";
        for (let i = 0; i < 6; i++) magic += String.fromCharCode(view.getUint8(i));
        if (magic !== "!j106\\\\") return null;
        pos += 6;

        function readUTF() {{
            const len = view.getUint32(pos); pos += 4;
            let s = "";
            for (let i = 0; i < len; i++) s += String.fromCharCode(view.getUint8(pos + i));
            pos += len;
            try {{ return decodeURIComponent(escape(s)); }} catch(e) {{ return s; }}
        }}

        const header = readUTF();
        if (header !== "Library") return null;
        const major = view.getUint32(pos); pos += 4;
        const minor = view.getUint32(pos); pos += 4;
        const libName = readUTF();
        pos += 4;
        const numPatches = view.getUint32(pos); pos += 4;
        pos += 8; // flags
        pos += 8; // crc

        const key = [34, 68, 40, 103, 61, 43, 49, 45, 71, 124, 6, 50, 46, 50, 32, 104];
        const patches = [];

        for (let idx = 0; idx < numPatches; idx++) {{
            const pName = readUTF();
            if (minor === 1) {{
                readUTF(); // uuid
            }}
            pos += 8; // pos_tag
            const salt = ((idx + 79) * 137 - 31);
            const params = [];
            for (let k = 0; k < 18; k++) {{
                const sVal = view.getInt16(pos); pos += 2;
                const kVal = key[((k + salt) % 16 + 16) % 16];
                params.push(Math.trunc(sVal / kVal));
            }}
            pos += 8; // crc

            patches.push({{
                name: pName,
                sliders: params.slice(0, 16),
                sw1: params[16],
                sw2: params[17]
            }});
        }}
        return {{ libName, patches }};
    }} catch (e) {{
        console.error("Error parsing .106 file:", e);
        return null;
    }}
}}

function handleImportedFile(file) {{
    const reader = new FileReader();
    const fname = file.name.toLowerCase();
    const baseTargetSlot = currentBank === 'UB' ? 192 : 128;

    if (fname.endsWith('.json')) {{
        reader.onload = (e) => {{
            try {{
                const data = JSON.parse(e.target.result);
                if (Array.isArray(data) && data.length > 0) {{
                    let importedCount = 0;
                    data.forEach((p, idx) => {{
                        const targetSlot = baseTargetSlot + idx;
                        if (targetSlot < 256) {{
                            // Check Jarvik7 lib.json format (has p.data with 18 values)
                            if (p.data && Array.isArray(p.data) && p.data.length === 18) {{
                                allPresets[targetSlot] = {{
                                    name: p.name || `${{indexToSlot(targetSlot)}} Patch`,
                                    sliders: p.data.slice(0, 16),
                                    sw1: p.data[16],
                                    sw2: p.data[17]
                                }};
                                importedCount++;
                            }}
                            // Or standard format (has p.sliders with 16 values)
                            else if (p.sliders && p.sliders.length === 16) {{
                                allPresets[targetSlot] = p;
                                importedCount++;
                            }}
                        }}
                    }});
                    showToast(`Imported ${{importedCount}} patches into User Bank! Factory presets preserved.`, 'success');
                    selectSlot(baseTargetSlot);
                }}
            }} catch (err) {{
                showToast('Invalid JSON preset file: ' + err.message, 'error');
            }}
        }};
        reader.readAsText(file);
    }} else {{
        reader.onload = (e) => {{
            const buf = e.target.result;
            // Check Jarvik7 .106 library format
            const j106 = parseJ106Buffer(buf);
            if (j106 && j106.patches && j106.patches.length > 0) {{
                j106.patches.forEach((p, idx) => {{
                    const targetSlot = baseTargetSlot + idx;
                    if (targetSlot < 256) allPresets[targetSlot] = p;
                }});
                showToast(`Loaded ${{j106.patches.length}} patches from .106 library ("${{j106.libName}}") into User Bank! Factory presets preserved.`, 'success');
                selectSlot(baseTargetSlot);
                return;
            }}

            // Standard SysEx buffer
            const patches = parseSyxBuffer(buf);
            if (patches.length === 0) {{
                showToast('No valid synthesizer patches or .106 library found in file.', 'error');
                return;
            }}
            if (patches.length === 1) {{
                const targetSlot = selectedSlot >= 128 ? selectedSlot : 128;
                allPresets[targetSlot] = patches[0].preset;
                showToast(`Loaded single patch "${{patches[0].preset.name}}" into User slot ${{indexToSlot(targetSlot)}}`, 'success');
                selectSlot(targetSlot);
            }} else {{
                patches.forEach((item, idx) => {{
                    const targetSlot = baseTargetSlot + idx;
                    if (targetSlot < 256) allPresets[targetSlot] = item.preset;
                }});
                showToast(`Loaded ${{patches.length}} patches into User Bank! Factory presets preserved.`, 'success');
                selectSlot(baseTargetSlot);
            }}
        }};
        reader.readAsArrayBuffer(file);
    }}
}}

// Export Functions
function downloadBlob(blob, filename) {{
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    setTimeout(() => {{
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
    }}, 200);
}}

function exportSinglePatchSyx() {{
    const p = readUIToPreset();
    const ch = getMidiChannel();
    const nameBytes = encodePatchName(p.name);
    const bankOpt = Math.floor(selectedSlot / 64) + 1;
    const localSlot = selectedSlot % 64;
    const chByte = ((bankOpt << 4) & 0x70) | (ch & 0x0F);
    // Standard 40-byte extended SysEx
    const bytes = [
        0xF0, 0x41, 0x30, chByte, localSlot & 0x7F,
        ...p.sliders,
        p.sw1,
        p.sw2,
        ...nameBytes,
        0xF7
    ];
    const blob = new Blob([new Uint8Array(bytes)], {{ type: 'application/octet-stream' }});
    downloadBlob(blob, `${{indexToSlot(selectedSlot)}}_${{p.name.replace(/[^a-zA-Z0-9_-]/g, '_')}}.syx`);
    showToast(`Exported ${{indexToSlot(selectedSlot)}} .syx`, 'success');
}}

function exportBankSyx() {{
    const ch = getMidiChannel();
    const startIdx = getBankStartIdx(currentBank);
    const endIdx = startIdx + 64;
    const bankOpt = Math.floor(startIdx / 64) + 1;
    const allBytes = [];

    for (let i = startIdx; i < endIdx; i++) {{
        const p = allPresets[i];
        const localSlot = i % 64;
        const chByte = ((bankOpt << 4) & 0x70) | (ch & 0x0F);
        const nameBytes = encodePatchName(p.name);
        const msg = [
            0xF0, 0x41, 0x30, chByte, localSlot & 0x7F,
            ...p.sliders,
            p.sw1,
            p.sw2,
            ...nameBytes,
            0xF7
        ];
        allBytes.push(...msg);
    }}
    const blob = new Blob([new Uint8Array(allBytes)], {{ type: 'application/octet-stream' }});
    downloadBlob(blob, `FM106_BANK_${{currentBank}}.syx`);
    showToast(`Exported Bank ${{currentBank}} .syx (64 patches)`, 'success');
}}

function exportLibraryJson() {{
    if (selectedSlot >= 128) {{
        allPresets[selectedSlot] = readUIToPreset();
    }}
    const jsonStr = JSON.stringify(allPresets, null, 2);
    const blob = new Blob([jsonStr], {{ type: 'application/json' }});
    downloadBlob(blob, 'FM106_Library_256.json');
    showToast('Exported full library backup JSON (256 patches)', 'success');
}}

// Setup UI Event Listeners
function setupEventListeners() {{
    // Bank Tabs
    document.getElementById('tab-bank-fa').addEventListener('click', () => {{
        currentBank = 'FA';
        updateBankTabs();
        selectSlot(0);
    }});
    document.getElementById('tab-bank-fb').addEventListener('click', () => {{
        currentBank = 'FB';
        updateBankTabs();
        selectSlot(64);
    }});
    document.getElementById('tab-bank-ua').addEventListener('click', () => {{
        currentBank = 'UA';
        updateBankTabs();
        selectSlot(128);
    }});
    document.getElementById('tab-bank-ub').addEventListener('click', () => {{
        currentBank = 'UB';
        updateBankTabs();
        selectSlot(192);
    }});
    document.getElementById('btn-copy-to-user').addEventListener('click', () => {{
        copyCurrentToUser();
    }});

    // Search filter
    document.getElementById('search-input').addEventListener('input', (e) => {{
        renderPresetList(e.target.value);
    }});

    // Patch Name input
    document.getElementById('current-patch-name').addEventListener('input', (e) => {{
        if (selectedSlot >= 128) {{
            allPresets[selectedSlot].name = e.target.value;
            const itemEl = document.querySelector(`.preset-item[data-idx="${{selectedSlot}}"] .preset-item-name`);
            if (itemEl) itemEl.textContent = e.target.value;
        }}
    }});

    // MIDI Connection
    document.getElementById('btn-midi-connect').addEventListener('click', () => connectMIDI());

    // Fader sliders
    const faderControls = document.querySelectorAll('.fader-control');
    faderControls.forEach(fc => {{
        const ctrlId = parseInt(fc.dataset.ctrl, 10);
        const input = fc.querySelector('.fader-input');
        input.addEventListener('input', () => {{
            updateFaderCap(ctrlId, parseInt(input.value, 10));
            sendIPR(ctrlId, parseInt(input.value, 10));
        }});
    }});

    // Octave Switch
    document.querySelectorAll('#sw-octave .switch-opt').forEach(opt => {{
        opt.addEventListener('click', () => {{
            setSwitchGroupActive('sw-octave', 'oct', parseInt(opt.dataset.oct, 10));
            const p = readUIToPreset();
            sendIPR(0x10, p.sw1);
        }});
    }});

    // Pulse Switch
    document.querySelectorAll('#sw-pulse .switch-opt').forEach(opt => {{
        opt.addEventListener('click', () => {{
            setSwitchGroupActive('sw-pulse', 'val', parseInt(opt.dataset.val, 10));
            const p = readUIToPreset();
            sendIPR(0x10, p.sw1);
        }});
    }});

    // Saw Switch
    document.querySelectorAll('#sw-saw .switch-opt').forEach(opt => {{
        opt.addEventListener('click', () => {{
            setSwitchGroupActive('sw-saw', 'val', parseInt(opt.dataset.val, 10));
            const p = readUIToPreset();
            sendIPR(0x10, p.sw1);
        }});
    }});

    // Chorus Buttons
    document.querySelectorAll('.chorus-btn').forEach(btn => {{
        btn.addEventListener('click', () => {{
            setChorusActive(parseInt(btn.dataset.chorus, 10));
            const p = readUIToPreset();
            sendIPR(0x10, p.sw1);
        }});
    }});

    // PWM Mode Switch
    document.querySelectorAll('#sw-pwm-mode .switch-opt').forEach(opt => {{
        opt.addEventListener('click', () => {{
            setSwitchGroupActive('sw-pwm-mode', 'val', parseInt(opt.dataset.val, 10));
            const p = readUIToPreset();
            sendIPR(0x11, p.sw2);
        }});
    }});

    // VCF Polarity Switch
    document.querySelectorAll('#sw-vcf-polarity .switch-opt').forEach(opt => {{
        opt.addEventListener('click', () => {{
            setSwitchGroupActive('sw-vcf-polarity', 'val', parseInt(opt.dataset.val, 10));
            const p = readUIToPreset();
            sendIPR(0x11, p.sw2);
        }});
    }});

    // VCA Mode Switch
    document.querySelectorAll('#sw-vca-mode .switch-opt').forEach(opt => {{
        opt.addEventListener('click', () => {{
            setSwitchGroupActive('sw-vca-mode', 'val', parseInt(opt.dataset.val, 10));
            const p = readUIToPreset();
            sendIPR(0x11, p.sw2);
        }});
    }});

    // HPF Switch
    document.querySelectorAll('#sw-hpf .switch-opt').forEach(opt => {{
        opt.addEventListener('click', () => {{
            setSwitchGroupActive('sw-hpf', 'hpf', parseInt(opt.dataset.hpf, 10));
            const p = readUIToPreset();
            sendIPR(0x11, p.sw2);
        }});
    }});

    // Action Buttons
    document.getElementById('btn-audition').addEventListener('click', () => auditionCurrentPreset());
    document.getElementById('btn-save-slot').addEventListener('click', () => saveCurrentSlotToHardware());
    document.getElementById('btn-burn-flash').addEventListener('click', () => burnToHardwareFlash());
    document.getElementById('btn-sync-all-to-hw').addEventListener('click', () => sendAllToHardware());
    document.getElementById('btn-read-all-from-hw').addEventListener('click', () => readAllFromHardware());

    document.getElementById('btn-export-single').addEventListener('click', () => exportSinglePatchSyx());
    document.getElementById('btn-export-bank').addEventListener('click', () => exportBankSyx());
    document.getElementById('btn-export-json').addEventListener('click', () => exportLibraryJson());

    // Restore Factory Defaults (resets user banks to defaults)
    document.getElementById('btn-restore-factory').addEventListener('click', () => {{
        if (confirm('Restore User Banks to default starting sounds?')) {{
            for (let i = 0; i < 128; i++) {{
                const copy = JSON.parse(JSON.stringify(FACTORY_PRESETS[i]));
                const prefix = i < 64 ? 'UA' : 'UB';
                const subIdx = i % 64;
                const grp = Math.floor(subIdx / 8) + 1;
                const ptc = (subIdx % 8) + 1;
                copy.name = copy.name.replace(/^[AB]\d\d\s*/, `${{prefix}}${{grp}}${{ptc}} `);
                allPresets[128 + i] = copy;
            }}
            loadPresetToUI(allPresets[selectedSlot]);
            renderPresetList();
            showToast('User Banks reset to default sounds.', 'success');
        }}
    }});

    // Init patch
    document.getElementById('btn-init-patch').addEventListener('click', () => {{
        if (isFactorySlot(selectedSlot)) {{
            showToast('Factory Presets are read-only ROM. Click "Copy to User" to initialize and edit.', 'warning');
            return;
        }}
        allPresets[selectedSlot] = {{
            name: `${{indexToSlot(selectedSlot)}} Init Saw`,
            sliders: [0, 0, 0, 0, 0, 100, 0, 0, 0, 0, 100, 0, 30, 100, 20, 0],
            sw1: 0x12, // 8\', Saw on
            sw2: 0x10  // HPF Flat
        }};
        loadPresetToUI(allPresets[selectedSlot]);
        renderPresetList();
        auditionCurrentPreset();
        showToast('Initialized user patch to basic saw.', 'info');
    }});

    // File picker
    const filePicker = document.getElementById('file-picker');
    document.getElementById('btn-import-file').addEventListener('click', () => filePicker.click());
    filePicker.addEventListener('change', (e) => {{
        if (e.target.files.length > 0) {{
            handleImportedFile(e.target.files[0]);
            filePicker.value = '';
        }}
    }});

    // Drag and Drop
    const dropOverlay = document.getElementById('drop-overlay');
    window.addEventListener('dragenter', (e) => {{
        e.preventDefault();
        dropOverlay.classList.add('visible');
    }});
    dropOverlay.addEventListener('dragover', (e) => {{
        e.preventDefault();
    }});
    dropOverlay.addEventListener('dragleave', (e) => {{
        e.preventDefault();
        dropOverlay.classList.remove('visible');
    }});
    dropOverlay.addEventListener('drop', (e) => {{
        e.preventDefault();
        dropOverlay.classList.remove('visible');
        if (e.dataTransfer.files.length > 0) {{
            handleImportedFile(e.dataTransfer.files[0]);
        }}
    }});
}}

// Initialize App on DOM Load
window.addEventListener('DOMContentLoaded', () => {{
    setupEventListeners();
    initKeyboard();
    selectSlot(0);
    initMIDI();
}});
</script>
</body>
</html>
"""

os.makedirs("web", exist_ok=True)
with open("web/index.html", "w", encoding="utf-8") as f:
    f.write(html_template)

print(f"Generated web/index.html successfully (size: {len(html_template)} bytes)")
