// Material 3's controls, as Google's own web components draw them: the
// competitor's side of the controls' review (capture.mjs). Bundled by esbuild.
import '@fontsource/roboto/400.css';
import '@fontsource/roboto/500.css';
import '@material/web/button/filled-button.js';
import '@material/web/button/outlined-button.js';
import '@material/web/switch/switch.js';
import '@material/web/checkbox/checkbox.js';
import '@material/web/slider/slider.js';
import '@material/web/textfield/outlined-text-field.js';
import '@material/web/labs/segmentedbutton/outlined-segmented-button.js';
import '@material/web/labs/segmentedbuttonset/outlined-segmented-button-set.js';

// One control a page (?control=<name>), centred in a cell the size of ours.
const controls = {
  primary: '<md-filled-button id="it">Save changes</md-filled-button>',
  secondary: '<md-outlined-button id="it">Cancel</md-outlined-button>',
  danger: '<md-filled-button id="it" style="--md-filled-button-container-color:#b3261e;--md-filled-button-label-text-color:#fff;' +
          '--md-filled-button-hover-label-text-color:#fff;--md-filled-button-pressed-label-text-color:#fff;--md-filled-button-focus-label-text-color:#fff">Delete</md-filled-button>',
  toggle: '<label style="display:flex;align-items:center;gap:12px"><md-switch id="it" selected></md-switch>Sync</label>',
  checkbox: '<label style="display:flex;align-items:center;gap:8px"><md-checkbox id="it" checked touch-target="wrapper"></md-checkbox>Back up</label>',
  slider: '<md-slider id="it" value="60" style="width:170px"></md-slider>',
  segmented: '<md-outlined-segmented-button-set id="it"><md-outlined-segmented-button label="Day"></md-outlined-segmented-button>' +
             '<md-outlined-segmented-button label="Week" selected></md-outlined-segmented-button><md-outlined-segmented-button label="Month"></md-outlined-segmented-button>' +
             '</md-outlined-segmented-button-set>',
  'text-box': '<md-outlined-text-field id="it" placeholder="Search" style="width:170px"></md-outlined-text-field>',
};
const name = new URLSearchParams(location.search).get('control');
document.getElementById('cell').innerHTML = controls[name] ?? `<p>no control ${name}</p>`;
window.controlNames = Object.keys(controls);
customElements.whenDefined('md-filled-button').then(() => document.fonts.ready).then(() => { window.controlReady = true; });
