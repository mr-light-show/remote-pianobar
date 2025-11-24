(function(){const t=document.createElement("link").relList;if(t&&t.supports&&t.supports("modulepreload"))return;for(const n of document.querySelectorAll('link[rel="modulepreload"]'))i(n);new MutationObserver(n=>{for(const o of n)if(o.type==="childList")for(const r of o.addedNodes)r.tagName==="LINK"&&r.rel==="modulepreload"&&i(r)}).observe(document,{childList:!0,subtree:!0});function e(n){const o={};return n.integrity&&(o.integrity=n.integrity),n.referrerPolicy&&(o.referrerPolicy=n.referrerPolicy),n.crossOrigin==="use-credentials"?o.credentials="include":n.crossOrigin==="anonymous"?o.credentials="omit":o.credentials="same-origin",o}function i(n){if(n.ep)return;n.ep=!0;const o=e(n);fetch(n.href,o)}})();/**
 * @license
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */const G=globalThis,it=G.ShadowRoot&&(G.ShadyCSS===void 0||G.ShadyCSS.nativeShadow)&&"adoptedStyleSheets"in Document.prototype&&"replace"in CSSStyleSheet.prototype,ot=Symbol(),ut=new WeakMap;let At=class{constructor(t,e,i){if(this._$cssResult$=!0,i!==ot)throw Error("CSSResult is not constructable. Use `unsafeCSS` or `css` instead.");this.cssText=t,this.t=e}get styleSheet(){let t=this.o;const e=this.t;if(it&&t===void 0){const i=e!==void 0&&e.length===1;i&&(t=ut.get(e)),t===void 0&&((this.o=t=new CSSStyleSheet).replaceSync(this.cssText),i&&ut.set(e,t))}return t}toString(){return this.cssText}};const Ot=s=>new At(typeof s=="string"?s:s+"",void 0,ot),$=(s,...t)=>{const e=s.length===1?s[0]:t.reduce((i,n,o)=>i+(r=>{if(r._$cssResult$===!0)return r.cssText;if(typeof r=="number")return r;throw Error("Value passed to 'css' function must be a 'css' function result: "+r+". Use 'unsafeCSS' to pass non-literal values, but take care to ensure page security.")})(n)+s[o+1],s[0]);return new At(e,s,ot)},Mt=(s,t)=>{if(it)s.adoptedStyleSheets=t.map(e=>e instanceof CSSStyleSheet?e:e.styleSheet);else for(const e of t){const i=document.createElement("style"),n=G.litNonce;n!==void 0&&i.setAttribute("nonce",n),i.textContent=e.cssText,s.appendChild(i)}},dt=it?s=>s:s=>s instanceof CSSStyleSheet?(t=>{let e="";for(const i of t.cssRules)e+=i.cssText;return Ot(e)})(s):s;/**
 * @license
 * Copyright 2017 Google LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */const{is:Tt,defineProperty:Nt,getOwnPropertyDescriptor:zt,getOwnPropertyNames:Ut,getOwnPropertySymbols:Rt,getPrototypeOf:jt}=Object,A=globalThis,mt=A.trustedTypes,Dt=mt?mt.emptyScript:"",Y=A.reactiveElementPolyfillSupport,U=(s,t)=>s,F={toAttribute(s,t){switch(t){case Boolean:s=s?Dt:null;break;case Object:case Array:s=s==null?s:JSON.stringify(s)}return s},fromAttribute(s,t){let e=s;switch(t){case Boolean:e=s!==null;break;case Number:e=s===null?null:Number(s);break;case Object:case Array:try{e=JSON.parse(s)}catch{e=null}}return e}},rt=(s,t)=>!Tt(s,t),ft={attribute:!0,type:String,converter:F,reflect:!1,useDefault:!1,hasChanged:rt};Symbol.metadata??(Symbol.metadata=Symbol("metadata")),A.litPropertyMetadata??(A.litPropertyMetadata=new WeakMap);let P=class extends HTMLElement{static addInitializer(t){this._$Ei(),(this.l??(this.l=[])).push(t)}static get observedAttributes(){return this.finalize(),this._$Eh&&[...this._$Eh.keys()]}static createProperty(t,e=ft){if(e.state&&(e.attribute=!1),this._$Ei(),this.prototype.hasOwnProperty(t)&&((e=Object.create(e)).wrapped=!0),this.elementProperties.set(t,e),!e.noAccessor){const i=Symbol(),n=this.getPropertyDescriptor(t,i,e);n!==void 0&&Nt(this.prototype,t,n)}}static getPropertyDescriptor(t,e,i){const{get:n,set:o}=zt(this.prototype,t)??{get(){return this[e]},set(r){this[e]=r}};return{get:n,set(r){const l=n==null?void 0:n.call(this);o==null||o.call(this,r),this.requestUpdate(t,l,i)},configurable:!0,enumerable:!0}}static getPropertyOptions(t){return this.elementProperties.get(t)??ft}static _$Ei(){if(this.hasOwnProperty(U("elementProperties")))return;const t=jt(this);t.finalize(),t.l!==void 0&&(this.l=[...t.l]),this.elementProperties=new Map(t.elementProperties)}static finalize(){if(this.hasOwnProperty(U("finalized")))return;if(this.finalized=!0,this._$Ei(),this.hasOwnProperty(U("properties"))){const e=this.properties,i=[...Ut(e),...Rt(e)];for(const n of i)this.createProperty(n,e[n])}const t=this[Symbol.metadata];if(t!==null){const e=litPropertyMetadata.get(t);if(e!==void 0)for(const[i,n]of e)this.elementProperties.set(i,n)}this._$Eh=new Map;for(const[e,i]of this.elementProperties){const n=this._$Eu(e,i);n!==void 0&&this._$Eh.set(n,e)}this.elementStyles=this.finalizeStyles(this.styles)}static finalizeStyles(t){const e=[];if(Array.isArray(t)){const i=new Set(t.flat(1/0).reverse());for(const n of i)e.unshift(dt(n))}else t!==void 0&&e.push(dt(t));return e}static _$Eu(t,e){const i=e.attribute;return i===!1?void 0:typeof i=="string"?i:typeof t=="string"?t.toLowerCase():void 0}constructor(){super(),this._$Ep=void 0,this.isUpdatePending=!1,this.hasUpdated=!1,this._$Em=null,this._$Ev()}_$Ev(){var t;this._$ES=new Promise(e=>this.enableUpdating=e),this._$AL=new Map,this._$E_(),this.requestUpdate(),(t=this.constructor.l)==null||t.forEach(e=>e(this))}addController(t){var e;(this._$EO??(this._$EO=new Set)).add(t),this.renderRoot!==void 0&&this.isConnected&&((e=t.hostConnected)==null||e.call(t))}removeController(t){var e;(e=this._$EO)==null||e.delete(t)}_$E_(){const t=new Map,e=this.constructor.elementProperties;for(const i of e.keys())this.hasOwnProperty(i)&&(t.set(i,this[i]),delete this[i]);t.size>0&&(this._$Ep=t)}createRenderRoot(){const t=this.shadowRoot??this.attachShadow(this.constructor.shadowRootOptions);return Mt(t,this.constructor.elementStyles),t}connectedCallback(){var t;this.renderRoot??(this.renderRoot=this.createRenderRoot()),this.enableUpdating(!0),(t=this._$EO)==null||t.forEach(e=>{var i;return(i=e.hostConnected)==null?void 0:i.call(e)})}enableUpdating(t){}disconnectedCallback(){var t;(t=this._$EO)==null||t.forEach(e=>{var i;return(i=e.hostDisconnected)==null?void 0:i.call(e)})}attributeChangedCallback(t,e,i){this._$AK(t,i)}_$ET(t,e){var o;const i=this.constructor.elementProperties.get(t),n=this.constructor._$Eu(t,i);if(n!==void 0&&i.reflect===!0){const r=(((o=i.converter)==null?void 0:o.toAttribute)!==void 0?i.converter:F).toAttribute(e,i.type);this._$Em=t,r==null?this.removeAttribute(n):this.setAttribute(n,r),this._$Em=null}}_$AK(t,e){var o,r;const i=this.constructor,n=i._$Eh.get(t);if(n!==void 0&&this._$Em!==n){const l=i.getPropertyOptions(n),a=typeof l.converter=="function"?{fromAttribute:l.converter}:((o=l.converter)==null?void 0:o.fromAttribute)!==void 0?l.converter:F;this._$Em=n;const c=a.fromAttribute(e,l.type);this[n]=c??((r=this._$Ej)==null?void 0:r.get(n))??c,this._$Em=null}}requestUpdate(t,e,i){var n;if(t!==void 0){const o=this.constructor,r=this[t];if(i??(i=o.getPropertyOptions(t)),!((i.hasChanged??rt)(r,e)||i.useDefault&&i.reflect&&r===((n=this._$Ej)==null?void 0:n.get(t))&&!this.hasAttribute(o._$Eu(t,i))))return;this.C(t,e,i)}this.isUpdatePending===!1&&(this._$ES=this._$EP())}C(t,e,{useDefault:i,reflect:n,wrapped:o},r){i&&!(this._$Ej??(this._$Ej=new Map)).has(t)&&(this._$Ej.set(t,r??e??this[t]),o!==!0||r!==void 0)||(this._$AL.has(t)||(this.hasUpdated||i||(e=void 0),this._$AL.set(t,e)),n===!0&&this._$Em!==t&&(this._$Eq??(this._$Eq=new Set)).add(t))}async _$EP(){this.isUpdatePending=!0;try{await this._$ES}catch(e){Promise.reject(e)}const t=this.scheduleUpdate();return t!=null&&await t,!this.isUpdatePending}scheduleUpdate(){return this.performUpdate()}performUpdate(){var i;if(!this.isUpdatePending)return;if(!this.hasUpdated){if(this.renderRoot??(this.renderRoot=this.createRenderRoot()),this._$Ep){for(const[o,r]of this._$Ep)this[o]=r;this._$Ep=void 0}const n=this.constructor.elementProperties;if(n.size>0)for(const[o,r]of n){const{wrapped:l}=r,a=this[o];l!==!0||this._$AL.has(o)||a===void 0||this.C(o,void 0,r,a)}}let t=!1;const e=this._$AL;try{t=this.shouldUpdate(e),t?(this.willUpdate(e),(i=this._$EO)==null||i.forEach(n=>{var o;return(o=n.hostUpdate)==null?void 0:o.call(n)}),this.update(e)):this._$EM()}catch(n){throw t=!1,this._$EM(),n}t&&this._$AE(e)}willUpdate(t){}_$AE(t){var e;(e=this._$EO)==null||e.forEach(i=>{var n;return(n=i.hostUpdated)==null?void 0:n.call(i)}),this.hasUpdated||(this.hasUpdated=!0,this.firstUpdated(t)),this.updated(t)}_$EM(){this._$AL=new Map,this.isUpdatePending=!1}get updateComplete(){return this.getUpdateComplete()}getUpdateComplete(){return this._$ES}shouldUpdate(t){return!0}update(t){this._$Eq&&(this._$Eq=this._$Eq.forEach(e=>this._$ET(e,this[e]))),this._$EM()}updated(t){}firstUpdated(t){}};P.elementStyles=[],P.shadowRootOptions={mode:"open"},P[U("elementProperties")]=new Map,P[U("finalized")]=new Map,Y==null||Y({ReactiveElement:P}),(A.reactiveElementVersions??(A.reactiveElementVersions=[])).push("2.1.1");/**
 * @license
 * Copyright 2017 Google LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */const R=globalThis,Q=R.trustedTypes,gt=Q?Q.createPolicy("lit-html",{createHTML:s=>s}):void 0,St="$lit$",x=`lit$${Math.random().toFixed(9).slice(2)}$`,Ct="?"+x,Lt=`<${Ct}>`,k=document,j=()=>k.createComment(""),D=s=>s===null||typeof s!="object"&&typeof s!="function",at=Array.isArray,Ht=s=>at(s)||typeof(s==null?void 0:s[Symbol.iterator])=="function",tt=`[ 	
\f\r]`,z=/<(?:(!--|\/[^a-zA-Z])|(\/?[a-zA-Z][^>\s]*)|(\/?$))/g,vt=/-->/g,bt=/>/g,S=RegExp(`>|${tt}(?:([^\\s"'>=/]+)(${tt}*=${tt}*(?:[^ 	
\f\r"'\`<>=]|("|')|))|$)`,"g"),yt=/'/g,$t=/"/g,Et=/^(?:script|style|textarea|title)$/i,It=s=>(t,...e)=>({_$litType$:s,strings:t,values:e}),m=It(1),O=Symbol.for("lit-noChange"),u=Symbol.for("lit-nothing"),_t=new WeakMap,C=k.createTreeWalker(k,129);function kt(s,t){if(!at(s)||!s.hasOwnProperty("raw"))throw Error("invalid template strings array");return gt!==void 0?gt.createHTML(t):t}const Bt=(s,t)=>{const e=s.length-1,i=[];let n,o=t===2?"<svg>":t===3?"<math>":"",r=z;for(let l=0;l<e;l++){const a=s[l];let c,p,h=-1,y=0;for(;y<a.length&&(r.lastIndex=y,p=r.exec(a),p!==null);)y=r.lastIndex,r===z?p[1]==="!--"?r=vt:p[1]!==void 0?r=bt:p[2]!==void 0?(Et.test(p[2])&&(n=RegExp("</"+p[2],"g")),r=S):p[3]!==void 0&&(r=S):r===S?p[0]===">"?(r=n??z,h=-1):p[1]===void 0?h=-2:(h=r.lastIndex-p[2].length,c=p[1],r=p[3]===void 0?S:p[3]==='"'?$t:yt):r===$t||r===yt?r=S:r===vt||r===bt?r=z:(r=S,n=void 0);const w=r===S&&s[l+1].startsWith("/>")?" ":"";o+=r===z?a+Lt:h>=0?(i.push(c),a.slice(0,h)+St+a.slice(h)+x+w):a+x+(h===-2?l:w)}return[kt(s,o+(s[e]||"<?>")+(t===2?"</svg>":t===3?"</math>":"")),i]};class L{constructor({strings:t,_$litType$:e},i){let n;this.parts=[];let o=0,r=0;const l=t.length-1,a=this.parts,[c,p]=Bt(t,e);if(this.el=L.createElement(c,i),C.currentNode=this.el.content,e===2||e===3){const h=this.el.content.firstChild;h.replaceWith(...h.childNodes)}for(;(n=C.nextNode())!==null&&a.length<l;){if(n.nodeType===1){if(n.hasAttributes())for(const h of n.getAttributeNames())if(h.endsWith(St)){const y=p[r++],w=n.getAttribute(h).split(x),V=/([.?@])?(.*)/.exec(y);a.push({type:1,index:o,name:V[2],strings:w,ctor:V[1]==="."?Wt:V[1]==="?"?Vt:V[1]==="@"?Gt:K}),n.removeAttribute(h)}else h.startsWith(x)&&(a.push({type:6,index:o}),n.removeAttribute(h));if(Et.test(n.tagName)){const h=n.textContent.split(x),y=h.length-1;if(y>0){n.textContent=Q?Q.emptyScript:"";for(let w=0;w<y;w++)n.append(h[w],j()),C.nextNode(),a.push({type:2,index:++o});n.append(h[y],j())}}}else if(n.nodeType===8)if(n.data===Ct)a.push({type:2,index:o});else{let h=-1;for(;(h=n.data.indexOf(x,h+1))!==-1;)a.push({type:7,index:o}),h+=x.length-1}o++}}static createElement(t,e){const i=k.createElement("template");return i.innerHTML=t,i}}function M(s,t,e=s,i){var r,l;if(t===O)return t;let n=i!==void 0?(r=e._$Co)==null?void 0:r[i]:e._$Cl;const o=D(t)?void 0:t._$litDirective$;return(n==null?void 0:n.constructor)!==o&&((l=n==null?void 0:n._$AO)==null||l.call(n,!1),o===void 0?n=void 0:(n=new o(s),n._$AT(s,e,i)),i!==void 0?(e._$Co??(e._$Co=[]))[i]=n:e._$Cl=n),n!==void 0&&(t=M(s,n._$AS(s,t.values),n,i)),t}class qt{constructor(t,e){this._$AV=[],this._$AN=void 0,this._$AD=t,this._$AM=e}get parentNode(){return this._$AM.parentNode}get _$AU(){return this._$AM._$AU}u(t){const{el:{content:e},parts:i}=this._$AD,n=((t==null?void 0:t.creationScope)??k).importNode(e,!0);C.currentNode=n;let o=C.nextNode(),r=0,l=0,a=i[0];for(;a!==void 0;){if(r===a.index){let c;a.type===2?c=new W(o,o.nextSibling,this,t):a.type===1?c=new a.ctor(o,a.name,a.strings,this,t):a.type===6&&(c=new Ft(o,this,t)),this._$AV.push(c),a=i[++l]}r!==(a==null?void 0:a.index)&&(o=C.nextNode(),r++)}return C.currentNode=k,n}p(t){let e=0;for(const i of this._$AV)i!==void 0&&(i.strings!==void 0?(i._$AI(t,i,e),e+=i.strings.length-2):i._$AI(t[e])),e++}}class W{get _$AU(){var t;return((t=this._$AM)==null?void 0:t._$AU)??this._$Cv}constructor(t,e,i,n){this.type=2,this._$AH=u,this._$AN=void 0,this._$AA=t,this._$AB=e,this._$AM=i,this.options=n,this._$Cv=(n==null?void 0:n.isConnected)??!0}get parentNode(){let t=this._$AA.parentNode;const e=this._$AM;return e!==void 0&&(t==null?void 0:t.nodeType)===11&&(t=e.parentNode),t}get startNode(){return this._$AA}get endNode(){return this._$AB}_$AI(t,e=this){t=M(this,t,e),D(t)?t===u||t==null||t===""?(this._$AH!==u&&this._$AR(),this._$AH=u):t!==this._$AH&&t!==O&&this._(t):t._$litType$!==void 0?this.$(t):t.nodeType!==void 0?this.T(t):Ht(t)?this.k(t):this._(t)}O(t){return this._$AA.parentNode.insertBefore(t,this._$AB)}T(t){this._$AH!==t&&(this._$AR(),this._$AH=this.O(t))}_(t){this._$AH!==u&&D(this._$AH)?this._$AA.nextSibling.data=t:this.T(k.createTextNode(t)),this._$AH=t}$(t){var o;const{values:e,_$litType$:i}=t,n=typeof i=="number"?this._$AC(t):(i.el===void 0&&(i.el=L.createElement(kt(i.h,i.h[0]),this.options)),i);if(((o=this._$AH)==null?void 0:o._$AD)===n)this._$AH.p(e);else{const r=new qt(n,this),l=r.u(this.options);r.p(e),this.T(l),this._$AH=r}}_$AC(t){let e=_t.get(t.strings);return e===void 0&&_t.set(t.strings,e=new L(t)),e}k(t){at(this._$AH)||(this._$AH=[],this._$AR());const e=this._$AH;let i,n=0;for(const o of t)n===e.length?e.push(i=new W(this.O(j()),this.O(j()),this,this.options)):i=e[n],i._$AI(o),n++;n<e.length&&(this._$AR(i&&i._$AB.nextSibling,n),e.length=n)}_$AR(t=this._$AA.nextSibling,e){var i;for((i=this._$AP)==null?void 0:i.call(this,!1,!0,e);t!==this._$AB;){const n=t.nextSibling;t.remove(),t=n}}setConnected(t){var e;this._$AM===void 0&&(this._$Cv=t,(e=this._$AP)==null||e.call(this,t))}}class K{get tagName(){return this.element.tagName}get _$AU(){return this._$AM._$AU}constructor(t,e,i,n,o){this.type=1,this._$AH=u,this._$AN=void 0,this.element=t,this.name=e,this._$AM=n,this.options=o,i.length>2||i[0]!==""||i[1]!==""?(this._$AH=Array(i.length-1).fill(new String),this.strings=i):this._$AH=u}_$AI(t,e=this,i,n){const o=this.strings;let r=!1;if(o===void 0)t=M(this,t,e,0),r=!D(t)||t!==this._$AH&&t!==O,r&&(this._$AH=t);else{const l=t;let a,c;for(t=o[0],a=0;a<o.length-1;a++)c=M(this,l[i+a],e,a),c===O&&(c=this._$AH[a]),r||(r=!D(c)||c!==this._$AH[a]),c===u?t=u:t!==u&&(t+=(c??"")+o[a+1]),this._$AH[a]=c}r&&!n&&this.j(t)}j(t){t===u?this.element.removeAttribute(this.name):this.element.setAttribute(this.name,t??"")}}class Wt extends K{constructor(){super(...arguments),this.type=3}j(t){this.element[this.name]=t===u?void 0:t}}class Vt extends K{constructor(){super(...arguments),this.type=4}j(t){this.element.toggleAttribute(this.name,!!t&&t!==u)}}class Gt extends K{constructor(t,e,i,n,o){super(t,e,i,n,o),this.type=5}_$AI(t,e=this){if((t=M(this,t,e,0)??u)===O)return;const i=this._$AH,n=t===u&&i!==u||t.capture!==i.capture||t.once!==i.once||t.passive!==i.passive,o=t!==u&&(i===u||n);n&&this.element.removeEventListener(this.name,this,i),o&&this.element.addEventListener(this.name,this,t),this._$AH=t}handleEvent(t){var e;typeof this._$AH=="function"?this._$AH.call(((e=this.options)==null?void 0:e.host)??this.element,t):this._$AH.handleEvent(t)}}class Ft{constructor(t,e,i){this.element=t,this.type=6,this._$AN=void 0,this._$AM=e,this.options=i}get _$AU(){return this._$AM._$AU}_$AI(t){M(this,t)}}const et=R.litHtmlPolyfillSupport;et==null||et(L,W),(R.litHtmlVersions??(R.litHtmlVersions=[])).push("3.3.1");const Qt=(s,t,e)=>{const i=(e==null?void 0:e.renderBefore)??t;let n=i._$litPart$;if(n===void 0){const o=(e==null?void 0:e.renderBefore)??null;i._$litPart$=n=new W(t.insertBefore(j(),o),o,void 0,e??{})}return n._$AI(s),n};/**
 * @license
 * Copyright 2017 Google LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */const E=globalThis;class b extends P{constructor(){super(...arguments),this.renderOptions={host:this},this._$Do=void 0}createRenderRoot(){var e;const t=super.createRenderRoot();return(e=this.renderOptions).renderBefore??(e.renderBefore=t.firstChild),t}update(t){const e=this.render();this.hasUpdated||(this.renderOptions.isConnected=this.isConnected),super.update(t),this._$Do=Qt(e,this.renderRoot,this.renderOptions)}connectedCallback(){var t;super.connectedCallback(),(t=this._$Do)==null||t.setConnected(!0)}disconnectedCallback(){var t;super.disconnectedCallback(),(t=this._$Do)==null||t.setConnected(!1)}render(){return O}}var xt;b._$litElement$=!0,b.finalized=!0,(xt=E.litElementHydrateSupport)==null||xt.call(E,{LitElement:b});const nt=E.litElementPolyfillSupport;nt==null||nt({LitElement:b});(E.litElementVersions??(E.litElementVersions=[])).push("4.2.1");/**
 * @license
 * Copyright 2017 Google LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */const _=s=>(t,e)=>{e!==void 0?e.addInitializer(()=>{customElements.define(s,t)}):customElements.define(s,t)};/**
 * @license
 * Copyright 2017 Google LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */const Jt={attribute:!0,type:String,converter:F,reflect:!1,hasChanged:rt},Kt=(s=Jt,t,e)=>{const{kind:i,metadata:n}=e;let o=globalThis.litPropertyMetadata.get(n);if(o===void 0&&globalThis.litPropertyMetadata.set(n,o=new Map),i==="setter"&&((s=Object.create(s)).wrapped=!0),o.set(e.name,s),i==="accessor"){const{name:r}=e;return{set(l){const a=t.get.call(this);t.set.call(this,l),this.requestUpdate(r,a,s)},init(l){return l!==void 0&&this.C(r,void 0,s,l),l}}}if(i==="setter"){const{name:r}=e;return function(l){const a=this[r];t.call(this,l),this.requestUpdate(r,a,s)}}throw Error("Unsupported decorator location: "+i)};function g(s){return(t,e)=>typeof e=="object"?Kt(s,t,e):((i,n,o)=>{const r=n.hasOwnProperty(o);return n.constructor.createProperty(o,i),r?Object.getOwnPropertyDescriptor(n,o):void 0})(s,t,e)}/**
 * @license
 * Copyright 2017 Google LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */function f(s){return g({...s,state:!0,attribute:!1})}class Xt{constructor(){this.listeners=new Map,this.connectionListeners=[],this.isConnected=!1,this.connect()}connect(){const e=`${window.location.protocol==="https:"?"wss:":"ws:"}//${window.location.host}/socket.io`;this.ws=new WebSocket(e,"socketio"),this.ws.onopen=()=>{console.log("WebSocket connected"),this.isConnected=!0,this.notifyConnectionChange(!0),this.emit("query",null)},this.ws.onmessage=i=>{try{const n=i.data;if(typeof n=="string"&&n.startsWith("2")){const o=n.substring(1),r=JSON.parse(o),l=r[0],a=r[1];console.log("Socket.IO event:",l,a);const c=this.listeners.get(l);c&&c.forEach(p=>p(a))}}catch(n){console.error("Failed to parse Socket.IO message:",n,i.data)}},this.ws.onerror=i=>{console.error("WebSocket error:",i)},this.ws.onclose=()=>{console.log("WebSocket disconnected"),this.isConnected=!1,this.notifyConnectionChange(!1)}}notifyConnectionChange(t){this.connectionListeners.forEach(e=>e(t))}onConnectionChange(t){this.connectionListeners.push(t),t(this.isConnected)}reconnect(){this.ws.readyState!==WebSocket.OPEN&&(console.log("Attempting to reconnect..."),this.ws&&this.ws.close(),this.connect())}on(t,e){this.listeners.has(t)||this.listeners.set(t,[]),this.listeners.get(t).push(e)}emit(t,e){if(this.ws.readyState===WebSocket.OPEN){const i="2"+JSON.stringify([t,e]);this.ws.send(i)}}disconnect(){this.ws.close()}}var Zt=Object.defineProperty,Yt=Object.getOwnPropertyDescriptor,lt=(s,t,e,i)=>{for(var n=i>1?void 0:i?Yt(t,e):t,o=s.length-1,r;o>=0;o--)(r=s[o])&&(n=(i?r(t,e,n):r(n))||n);return i&&n&&Zt(t,e,n),n};let H=class extends b{constructor(){super(...arguments),this.src="",this.alt="Album Art"}render(){return this.src?m`<img src="${this.src}" alt="${this.alt}">`:m`<div class="disc-icon">
               <span class="material-icons">album</span>
             </div>`}};H.styles=$`
    :host {
      display: block;
      max-width: 32rem;
      margin: 0 auto;
      aspect-ratio: 1 / 1;
    }
    
    img {
      width: 100%;
      height: 100%;
      object-fit: cover;
    }
    
    .disc-icon {
      width: 100%;
      height: 100%;
      display: flex;
      align-items: center;
      justify-content: center;
      background: var(--surface-color);
      color: var(--on-surface-color);
    }
    
    .material-icons {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 8rem;
      line-height: 1;
      letter-spacing: normal;
      text-transform: none;
      display: inline-block;
      white-space: nowrap;
      word-wrap: normal;
      direction: ltr;
      -webkit-font-feature-settings: 'liga';
      -moz-font-feature-settings: 'liga';
      font-feature-settings: 'liga';
      -webkit-font-smoothing: antialiased;
    }
  `;lt([g()],H.prototype,"src",2);lt([g()],H.prototype,"alt",2);H=lt([_("album-art")],H);var te=Object.defineProperty,ee=Object.getOwnPropertyDescriptor,ct=(s,t,e,i)=>{for(var n=i>1?void 0:i?ee(t,e):t,o=s.length-1,r;o>=0;o--)(r=s[o])&&(n=(i?r(t,e,n):r(n))||n);return i&&n&&te(t,e,n),n};let I=class extends b{constructor(){super(...arguments),this.current=0,this.total=0}formatTime(s){const t=Math.floor(s/60),e=s%60;return`${t}:${e.toString().padStart(2,"0")}`}get progress(){return this.total>0?this.current/this.total*100:0}render(){return m`
      <div class="progress-track">
        <div class="progress-fill" style="width: ${this.progress}%"></div>
      </div>
      <div class="time-display">
        <span>${this.formatTime(this.current)}</span>
        <span>${this.formatTime(this.total)}</span>
      </div>
    `}};I.styles=$`
    :host {
      display: block;
      max-width: 32rem;
      margin: 1rem auto;
      padding: 0 1rem;
    }
    
    .progress-track {
      height: 4px;
      background: var(--surface-variant);
      border-radius: 2px;
      overflow: hidden;
      margin-bottom: 0.5rem;
    }
    
    .progress-fill {
      height: 100%;
      background: var(--primary-color);
      transition: width 0.3s ease;
    }
    
    .time-display {
      display: flex;
      justify-content: space-between;
      font-size: 0.875rem;
      color: var(--on-surface-variant);
    }
  `;ct([g({type:Number})],I.prototype,"current",2);ct([g({type:Number})],I.prototype,"total",2);I=ct([_("progress-bar")],I);var ne=Object.defineProperty,se=Object.getOwnPropertyDescriptor,Pt=(s,t,e,i)=>{for(var n=i>1?void 0:i?se(t,e):t,o=s.length-1,r;o>=0;o--)(r=s[o])&&(n=(i?r(t,e,n):r(n))||n);return i&&n&&ne(t,e,n),n};let J=class extends b{constructor(){super(...arguments),this.playing=!1}handlePlay(){this.dispatchEvent(new CustomEvent("play"))}handleNext(){this.dispatchEvent(new CustomEvent("next"))}render(){return m`
      <button @click=${this.handlePlay} class="primary" title="${this.playing?"Pause":"Play"}">
        <span class="material-icons">${this.playing?"pause":"play_arrow"}</span>
      </button>
      <button @click=${this.handleNext} title="Next Song">
        <span class="material-icons">skip_next</span>
      </button>
    `}};J.styles=$`
    :host {
      display: flex;
      justify-content: center;
      gap: 1rem;
      margin: 2rem 0;
    }
    
    button {
      width: 56px;
      height: 56px;
      border-radius: 50%;
      border: none;
      background: var(--surface-variant);
      color: var(--on-surface);
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      transition: all 0.2s;
    }
    
    button:hover {
      background: var(--primary-container);
      color: var(--on-primary-container);
    }
    
    button.primary {
      background: var(--primary-color);
      color: var(--on-primary);
    }
    
    button.primary:hover {
      background: var(--primary-dark);
    }
    
    .material-icons {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 28px;
      line-height: 1;
      letter-spacing: normal;
      text-transform: none;
      display: inline-block;
      white-space: nowrap;
      word-wrap: normal;
      direction: ltr;
      -webkit-font-feature-settings: 'liga';
      -moz-font-feature-settings: 'liga';
      font-feature-settings: 'liga';
      -webkit-font-smoothing: antialiased;
    }
  `;Pt([g({type:Boolean})],J.prototype,"playing",2);J=Pt([_("playback-controls")],J);var ie=Object.defineProperty,oe=Object.getOwnPropertyDescriptor,ht=(s,t,e,i)=>{for(var n=i>1?void 0:i?oe(t,e):t,o=s.length-1,r;o>=0;o--)(r=s[o])&&(n=(i?r(t,e,n):r(n))||n);return i&&n&&ie(t,e,n),n};function wt(s,t=10){if(s<=50){const e=s/50;return-40*Math.pow(1-e,2)}else{const e=(s-50)/50;return t*e}}function re(s,t=10){return s<=0?Math.sqrt(1-s/-40)*50:50+s/t*50}let B=class extends b{constructor(){super(...arguments),this.volume=50,this.maxGain=10}handleVolumeChange(s){const t=s.target,e=parseInt(t.value);this.volume=e;const i=Math.round(wt(e,this.maxGain));this.dispatchEvent(new CustomEvent("volume-change",{detail:{percent:e,db:i}}))}updateFromDb(s){this.volume=Math.round(re(s,this.maxGain))}render(){const s=Math.round(wt(this.volume,this.maxGain)),t=s>=0?`+${s}`:`${s}`;return m`
      <span class="material-icons">volume_up</span>
      <input 
        type="range" 
        min="0" 
        max="100" 
        step="1"
        .value="${this.volume}"
        @input=${this.handleVolumeChange}
      >
      <span class="volume-value">${this.volume}% (${t}dB)</span>
    `}};B.styles=$`
    :host {
      display: flex;
      align-items: center;
      gap: 1rem;
      max-width: 32rem;
      margin: 0 auto;
      padding: 0 2rem;
    }
    
    .material-icons {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 24px;
      line-height: 1;
      letter-spacing: normal;
      text-transform: none;
      display: inline-block;
      white-space: nowrap;
      word-wrap: normal;
      direction: ltr;
      color: var(--on-surface);
      -webkit-font-feature-settings: 'liga';
      -moz-font-feature-settings: 'liga';
      font-feature-settings: 'liga';
      -webkit-font-smoothing: antialiased;
    }
    
    input[type="range"] {
      flex: 1;
      height: 4px;
      -webkit-appearance: none;
      appearance: none;
      background: var(--surface-variant);
      border-radius: 2px;
      outline: none;
    }
    
    input[type="range"]::-webkit-slider-thumb {
      -webkit-appearance: none;
      appearance: none;
      width: 16px;
      height: 16px;
      border-radius: 50%;
      background: var(--primary-color);
      cursor: pointer;
    }
    
    input[type="range"]::-moz-range-thumb {
      width: 16px;
      height: 16px;
      border-radius: 50%;
      background: var(--primary-color);
      cursor: pointer;
      border: none;
    }
    
    .volume-value {
      min-width: 3rem;
      text-align: right;
      color: var(--on-surface-variant);
    }
  `;ht([g({type:Number})],B.prototype,"volume",2);ht([g({type:Number})],B.prototype,"maxGain",2);B=ht([_("volume-control")],B);var ae=Object.getOwnPropertyDescriptor,le=(s,t,e,i)=>{for(var n=i>1?void 0:i?ae(t,e):t,o=s.length-1,r;o>=0;o--)(r=s[o])&&(n=r(n)||n);return n};let st=class extends b{handleReconnect(){this.dispatchEvent(new CustomEvent("reconnect"))}render(){return m`
      <button @click=${this.handleReconnect} title="Reconnect">
        <span class="material-icons">sync</span>
      </button>
    `}};st.styles=$`
    :host {
      display: flex;
      justify-content: center;
      margin: 2rem 0;
    }
    
    button {
      width: 56px;
      height: 56px;
      border-radius: 50%;
      border: none;
      background: var(--error);
      color: var(--on-error);
      cursor: pointer;
      display: flex;
      align-items: center;
      justify-content: center;
      transition: all 0.2s;
      animation: pulse 2s infinite;
    }
    
    button:hover {
      background: var(--error-dark);
      animation: none;
    }
    
    @keyframes pulse {
      0%, 100% {
        opacity: 1;
      }
      50% {
        opacity: 0.6;
      }
    }
    
    .material-icons {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 28px;
      line-height: 1;
      letter-spacing: normal;
      text-transform: none;
      display: inline-block;
      white-space: nowrap;
      word-wrap: normal;
      direction: ltr;
      -webkit-font-feature-settings: 'liga';
      -moz-font-feature-settings: 'liga';
      font-feature-settings: 'liga';
      -webkit-font-smoothing: antialiased;
    }
  `;st=le([_("reconnect-button")],st);var ce=Object.defineProperty,he=Object.getOwnPropertyDescriptor,pt=(s,t,e,i)=>{for(var n=i>1?void 0:i?he(t,e):t,o=s.length-1,r;o>=0;o--)(r=s[o])&&(n=(i?r(t,e,n):r(n))||n);return i&&n&&ce(t,e,n),n};let q=class extends b{constructor(){super(...arguments),this.rating=0,this.menuOpen=!1,this.handleClickOutside=s=>{this.menuOpen&&!s.composedPath().includes(this)&&(this.menuOpen=!1)}}connectedCallback(){super.connectedCallback(),document.addEventListener("click",this.handleClickOutside)}disconnectedCallback(){super.disconnectedCallback(),document.removeEventListener("click",this.handleClickOutside)}toggleMenu(s){s&&s.stopPropagation(),this.menuOpen=!this.menuOpen}closeMenu(){this.menuOpen=!1}handleLove(){this.dispatchEvent(new CustomEvent("love")),this.closeMenu()}handleBan(){this.dispatchEvent(new CustomEvent("ban")),this.closeMenu()}handleTired(){this.dispatchEvent(new CustomEvent("tired")),this.closeMenu()}render(){return m`
      <div class="menu-popup ${this.menuOpen?"":"hidden"}">
        <button class="action-button love ${this.rating===1?"active":""}" @click=${this.handleLove}>
          <span class="material-icons">thumb_up</span>
          <span>Love Song</span>
        </button>
        <button class="action-button ban" @click=${this.handleBan}>
          <span class="material-icons">thumb_down</span>
          <span>Ban Song</span>
        </button>
        <button class="action-button tired" @click=${this.handleTired}>
          <span class="material-icons">snooze</span>
          <span>Snooze (1 month)</span>
        </button>
      </div>
    `}};q.styles=$`
    :host {
      position: relative;
      display: inline-block;
    }
    
    .menu-popup {
      position: absolute;
      bottom: 64px;
      left: 50%;
      transform: translateX(-50%);
      background: var(--surface);
      border-radius: 12px;
      box-shadow: 0 4px 16px rgba(0, 0, 0, 0.2);
      padding: 8px;
      display: flex;
      flex-direction: column;
      gap: 4px;
      z-index: 100;
      min-width: 180px;
    }
    
    .menu-popup.hidden {
      display: none;
    }
    
    .action-button {
      display: flex;
      align-items: center;
      gap: 12px;
      padding: 12px 16px;
      border: none;
      border-radius: 8px;
      background: transparent;
      color: var(--on-surface);
      cursor: pointer;
      transition: all 0.2s;
      text-align: left;
      font-size: 14px;
    }
    
    .action-button:hover {
      background: var(--surface-variant);
    }
    
    .action-button.love:hover {
      background: var(--success);
      color: var(--on-success);
    }
    
    .action-button.love.active {
      background: var(--success);
      color: var(--on-success);
    }
    
    .action-button.ban:hover {
      background: var(--error);
      color: var(--on-error);
    }
    
    .action-button.tired:hover {
      background: var(--warning);
      color: var(--on-warning);
    }
    
    .material-icons {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 20px;
      line-height: 1;
      letter-spacing: normal;
      text-transform: none;
      display: inline-block;
      white-space: nowrap;
      word-wrap: normal;
      direction: ltr;
      -webkit-font-feature-settings: 'liga';
      -moz-font-feature-settings: 'liga';
      font-feature-settings: 'liga';
      -webkit-font-smoothing: antialiased;
    }
  `;pt([g({type:Number})],q.prototype,"rating",2);pt([f()],q.prototype,"menuOpen",2);q=pt([_("song-actions-menu")],q);var pe=Object.defineProperty,ue=Object.getOwnPropertyDescriptor,X=(s,t,e,i)=>{for(var n=i>1?void 0:i?ue(t,e):t,o=s.length-1,r;o>=0;o--)(r=s[o])&&(n=(i?r(t,e,n):r(n))||n);return i&&n&&pe(t,e,n),n};let T=class extends b{constructor(){super(...arguments),this.stations=[],this.currentStation="",this.menuOpen=!1,this.handleClickOutside=s=>{this.menuOpen&&!s.composedPath().includes(this)&&(this.menuOpen=!1)}}connectedCallback(){super.connectedCallback(),document.addEventListener("click",this.handleClickOutside)}disconnectedCallback(){super.disconnectedCallback(),document.removeEventListener("click",this.handleClickOutside)}toggleMenu(s){s&&s.stopPropagation(),this.menuOpen=!this.menuOpen}closeMenu(){this.menuOpen=!1}handleStationClick(s){this.dispatchEvent(new CustomEvent("station-change",{detail:{station:s.id}})),this.closeMenu()}render(){return m`
      <div class="menu-popup ${this.menuOpen?"":"hidden"}">
        ${this.stations.map(s=>{const t=s.isQuickMix?"shuffle":"play_arrow",e=s.isQuickMixed&&!s.isQuickMix;return m`
            <button 
              class="station-button ${s.name===this.currentStation?"active":""}"
              @click=${()=>this.handleStationClick(s)}
            >
              <span class="material-icons station-icon-leading">${t}</span>
              <span class="station-name">${s.name}</span>
              ${e?m`<span class="material-icons quickmix-icon">shuffle</span>`:""}
            </button>
          `})}
      </div>
    `}};T.styles=$`
    :host {
      position: relative;
      display: inline-block;
    }
    
    .menu-popup {
      position: absolute;
      bottom: 64px;
      left: 50%;
      transform: translateX(-50%);
      background: var(--surface);
      border-radius: 12px;
      box-shadow: 0 4px 16px rgba(0, 0, 0, 0.2);
      padding: 8px;
      max-height: 400px;
      overflow-y: auto;
      z-index: 100;
      min-width: 250px;
      max-width: 90vw;
    }
    
    .menu-popup.hidden {
      display: none;
    }
    
    .station-button {
      display: flex;
      align-items: center;
      gap: 8px;
      width: 100%;
      padding: 12px 16px;
      margin: 2px 0;
      border: none;
      border-radius: 8px;
      background: transparent;
      color: var(--on-surface);
      text-align: left;
      cursor: pointer;
      transition: all 0.2s;
      font-size: 14px;
    }
    
    .station-button:hover {
      background: var(--surface-variant);
    }
    
    .station-button.active {
      background: var(--primary-container);
      color: var(--on-primary-container);
    }
    
    .station-button.active:hover {
      background: var(--primary-container);
    }
    
    .station-icon-leading {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 20px;
      line-height: 1;
      color: var(--on-surface);
      flex-shrink: 0;
      -webkit-font-feature-settings: 'liga';
      -moz-font-feature-settings: 'liga';
      font-feature-settings: 'liga';
      -webkit-font-smoothing: antialiased;
    }
    
    .station-name {
      flex: 1;
      text-align: left;
    }
    
    .quickmix-icon {
      font-family: 'Material Icons';
      font-weight: normal;
      font-style: normal;
      font-size: 18px;
      line-height: 1;
      margin-left: auto;
      color: var(--primary-color);
      flex-shrink: 0;
      -webkit-font-feature-settings: 'liga';
      -moz-font-feature-settings: 'liga';
      font-feature-settings: 'liga';
      -webkit-font-smoothing: antialiased;
    }
  `;X([g({type:Array})],T.prototype,"stations",2);X([g()],T.prototype,"currentStation",2);X([f()],T.prototype,"menuOpen",2);T=X([_("stations-popup")],T);var de=Object.defineProperty,me=Object.getOwnPropertyDescriptor,Z=(s,t,e,i)=>{for(var n=i>1?void 0:i?me(t,e):t,o=s.length-1,r;o>=0;o--)(r=s[o])&&(n=(i?r(t,e,n):r(n))||n);return i&&n&&de(t,e,n),n};let N=class extends b{constructor(){super(...arguments),this.stations=[],this.currentStation="",this.rating=0}toggleTools(s){const t=this.shadowRoot.querySelector("stations-popup");t&&t.closeMenu();const e=this.shadowRoot.querySelector("song-actions-menu");e&&e.toggleMenu(s)}toggleStations(s){const t=this.shadowRoot.querySelector("song-actions-menu");t&&t.closeMenu();const e=this.shadowRoot.querySelector("stations-popup");e&&e.toggleMenu(s)}handleLove(){this.dispatchEvent(new CustomEvent("love"))}handleBan(){this.dispatchEvent(new CustomEvent("ban"))}handleTired(){this.dispatchEvent(new CustomEvent("tired"))}handleStationChange(s){this.dispatchEvent(new CustomEvent("station-change",{detail:s.detail}))}render(){return m`
      <div class="button-group">
        <button 
          @click=${this.toggleTools}
          class="${this.rating===1?"loved":""}"
          title="${this.rating===1?"Loved":"Rate this song"}"
        >
          <span class="${this.rating===1?"material-icons":"material-icons-outlined"}">
            ${this.rating===1?"thumb_up":"thumbs_up_down"}
          </span>
        </button>
        <song-actions-menu
          rating="${this.rating}"
          @love=${this.handleLove}
          @ban=${this.handleBan}
          @tired=${this.handleTired}
        ></song-actions-menu>
      </div>
      
      <div class="button-group">
        <button @click=${this.toggleStations}>
          <span class="material-icons">radio</span>
          <span>Stations</span>
        </button>
        <stations-popup
          .stations="${this.stations}"
          currentStation="${this.currentStation}"
          @station-change=${this.handleStationChange}
        ></stations-popup>
      </div>
    `}};N.styles=$`
    :host {
      position: fixed;
      bottom: 0;
      left: 0;
      right: 0;
      background: var(--surface);
      border-top: 1px solid var(--outline);
      padding: 12px 16px;
      display: flex;
      justify-content: center;
      gap: 16px;
      z-index: 50;
      box-shadow: 0 -2px 8px rgba(0, 0, 0, 0.1);
    }
    
    button {
      display: flex;
      align-items: center;
      gap: 8px;
      padding: 10px 20px;
      border: none;
      border-radius: 20px;
      background: var(--surface-variant);
      color: var(--on-surface);
      cursor: pointer;
      transition: all 0.2s;
      font-size: 14px;
      font-weight: 500;
    }
    
    button:hover {
      background: var(--primary-container);
      color: var(--on-primary-container);
    }
    
    button.loved {
      color: #4CAF50;
    }
    
    button.loved:hover {
      background: rgba(76, 175, 80, 0.1);
      color: #4CAF50;
    }
    
    .button-group {
      position: relative;
      display: inline-block;
    }
    
    .material-icons,
    .material-icons-outlined {
      font-weight: normal;
      font-style: normal;
      font-size: 20px;
      line-height: 1;
      letter-spacing: normal;
      text-transform: none;
      display: inline-block;
      white-space: nowrap;
      word-wrap: normal;
      direction: ltr;
      -webkit-font-feature-settings: 'liga';
      -moz-font-feature-settings: 'liga';
      font-feature-settings: 'liga';
      -webkit-font-smoothing: antialiased;
    }
    
    .material-icons {
      font-family: 'Material Icons';
    }
    
    .material-icons-outlined {
      font-family: 'Material Icons Outlined';
    }
  `;Z([g({type:Array})],N.prototype,"stations",2);Z([g()],N.prototype,"currentStation",2);Z([g({type:Number})],N.prototype,"rating",2);N=Z([_("bottom-toolbar")],N);var fe=Object.defineProperty,ge=Object.getOwnPropertyDescriptor,v=(s,t,e,i)=>{for(var n=i>1?void 0:i?ge(t,e):t,o=s.length-1,r;o>=0;o--)(r=s[o])&&(n=(i?r(t,e,n):r(n))||n);return i&&n&&fe(t,e,n),n};let d=class extends b{constructor(){super(...arguments),this.socket=new Xt,this.connected=!1,this.albumArt="",this.songTitle="Not Playing",this.artistName="—",this.playing=!1,this.currentTime=0,this.totalTime=0,this.volume=0,this.maxGain=10,this.rating=0,this.stations=[],this.currentStation="",this.songStationName=""}connectedCallback(){super.connectedCallback(),this.setupSocketListeners(),this.setupConnectionListener()}setupConnectionListener(){this.socket.onConnectionChange(s=>{this.connected=s,s||(this.albumArt="",this.playing=!1,this.currentTime=0,this.totalTime=0)})}setupSocketListeners(){this.socket.on("start",s=>{this.albumArt=s.coverArt,this.songTitle=s.title,this.artistName=s.artist,this.totalTime=s.duration,this.playing=!0,this.rating=s.rating||0,this.songStationName=s.songStationName||""}),this.socket.on("stop",()=>{console.log("Received stop event"),this.playing=!1,this.currentTime=0,this.totalTime=0}),this.socket.on("progress",s=>{this.currentTime=s.elapsed,this.totalTime=s.duration}),this.socket.on("stations",s=>{this.stations=(Array.isArray(s)?s:[]).sort((t,e)=>t.isQuickMix?-1:e.isQuickMix?1:t.name.localeCompare(e.name))}),this.socket.on("process",s=>{var e;console.log("Received process event:",s),s.song?(this.albumArt=s.song.coverArt||"",this.songTitle=s.song.title||"Not Playing",this.artistName=s.song.artist||"—",this.totalTime=s.song.duration||0,this.playing=s.playing||!1,this.rating=s.song.rating||0,this.songStationName=s.song.songStationName||""):(this.albumArt="",this.songTitle="Not Playing",this.artistName="—",this.playing=!1,this.currentTime=0,this.totalTime=0,this.rating=0,this.songStationName=""),s.station&&(this.currentStation=s.station),s.volume!==void 0&&(this.volume=s.volume),s.maxGain!==void 0&&(this.maxGain=s.maxGain);const t=(e=this.shadowRoot)==null?void 0:e.querySelector("volume-control");t&&s.volume!==void 0&&t.updateFromDb(s.volume)})}handlePlayPause(){this.playing=!this.playing,this.socket.emit("action","playback.toggle")}handleNext(){this.socket.emit("action","playback.next")}handleLove(){this.socket.emit("action","song.love")}handleBan(){this.socket.emit("action","song.ban")}handleTired(){this.socket.emit("action","song.tired")}handleStationChange(s){const{station:t}=s.detail;this.socket.emit("station.change",t)}handleVolumeChange(s){const{percent:t,db:e}=s.detail;this.volume=e,this.socket.emit("action",{action:"volume.set",volume:t})}handleReconnect(){this.socket.reconnect()}render(){return m`
      <album-art 
        src="${this.connected?this.albumArt:""}"
      ></album-art>
      
      <div class="song-info">
        <h1>${this.connected?this.songTitle:"Disconnected"}</h1>
        <p class="artist">${this.connected?this.artistName:"—"}</p>
        ${this.songStationName?m`
          <p class="station-info">From: ${this.songStationName}</p>
        `:""}
      </div>
      
      <progress-bar 
        current="${this.connected?this.currentTime:0}"
        total="${this.connected?this.totalTime:0}"
      ></progress-bar>
      
      ${this.connected?m`
        <volume-control
          .volume="${50}"
          .maxGain="${this.maxGain}"
          @volume-change=${this.handleVolumeChange}
        ></volume-control>
        
        <playback-controls 
          ?playing="${this.playing}"
          @play=${this.handlePlayPause}
          @next=${this.handleNext}
        ></playback-controls>
        
        <bottom-toolbar
          .stations="${this.stations}"
          currentStation="${this.currentStation}"
          rating="${this.rating}"
          @love=${this.handleLove}
          @ban=${this.handleBan}
          @tired=${this.handleTired}
          @station-change=${this.handleStationChange}
        ></bottom-toolbar>
      `:m`
        <reconnect-button 
          @reconnect=${this.handleReconnect}
        ></reconnect-button>
      `}
    `}};d.styles=$`
    :host {
      display: block;
      min-height: 100vh;
      background: var(--background);
      color: var(--on-background);
      padding-bottom: 80px; /* Space for bottom toolbar */
    }
    
    .song-info {
      text-align: center;
      padding: 1rem 2rem;
      max-width: 32rem;
      margin: 0 auto;
    }
    
    h1 {
      font-size: 1.5rem;
      font-weight: 500;
      margin: 0.5rem 0;
    }
    
    .artist {
      color: var(--on-surface-variant);
      margin: 0;
    }
    
    .station-info {
      color: var(--on-surface-variant);
      font-size: 0.875rem;
      margin: 0.5rem 0 0 0;
    }
  `;v([f()],d.prototype,"connected",2);v([f()],d.prototype,"albumArt",2);v([f()],d.prototype,"songTitle",2);v([f()],d.prototype,"artistName",2);v([f()],d.prototype,"playing",2);v([f()],d.prototype,"currentTime",2);v([f()],d.prototype,"totalTime",2);v([f()],d.prototype,"volume",2);v([f()],d.prototype,"maxGain",2);v([f()],d.prototype,"rating",2);v([f()],d.prototype,"stations",2);v([f()],d.prototype,"currentStation",2);v([f()],d.prototype,"songStationName",2);d=v([_("pianobar-app")],d);
