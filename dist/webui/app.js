(function(){const t=document.createElement("link").relList;if(t&&t.supports&&t.supports("modulepreload"))return;for(const s of document.querySelectorAll('link[rel="modulepreload"]'))i(s);new MutationObserver(s=>{for(const o of s)if(o.type==="childList")for(const r of o.addedNodes)r.tagName==="LINK"&&r.rel==="modulepreload"&&i(r)}).observe(document,{childList:!0,subtree:!0});function e(s){const o={};return s.integrity&&(o.integrity=s.integrity),s.referrerPolicy&&(o.referrerPolicy=s.referrerPolicy),s.crossOrigin==="use-credentials"?o.credentials="include":s.crossOrigin==="anonymous"?o.credentials="omit":o.credentials="same-origin",o}function i(s){if(s.ep)return;s.ep=!0;const o=e(s);fetch(s.href,o)}})();/**
 * @license
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */const G=globalThis,it=G.ShadowRoot&&(G.ShadyCSS===void 0||G.ShadyCSS.nativeShadow)&&"adoptedStyleSheets"in Document.prototype&&"replace"in CSSStyleSheet.prototype,ot=Symbol(),ut=new WeakMap;let At=class{constructor(t,e,i){if(this._$cssResult$=!0,i!==ot)throw Error("CSSResult is not constructable. Use `unsafeCSS` or `css` instead.");this.cssText=t,this.t=e}get styleSheet(){let t=this.o;const e=this.t;if(it&&t===void 0){const i=e!==void 0&&e.length===1;i&&(t=ut.get(e)),t===void 0&&((this.o=t=new CSSStyleSheet).replaceSync(this.cssText),i&&ut.set(e,t))}return t}toString(){return this.cssText}};const Ot=n=>new At(typeof n=="string"?n:n+"",void 0,ot),$=(n,...t)=>{const e=n.length===1?n[0]:t.reduce((i,s,o)=>i+(r=>{if(r._$cssResult$===!0)return r.cssText;if(typeof r=="number")return r;throw Error("Value passed to 'css' function must be a 'css' function result: "+r+". Use 'unsafeCSS' to pass non-literal values, but take care to ensure page security.")})(s)+n[o+1],n[0]);return new At(e,n,ot)},Tt=(n,t)=>{if(it)n.adoptedStyleSheets=t.map(e=>e instanceof CSSStyleSheet?e:e.styleSheet);else for(const e of t){const i=document.createElement("style"),s=G.litNonce;s!==void 0&&i.setAttribute("nonce",s),i.textContent=e.cssText,n.appendChild(i)}},dt=it?n=>n:n=>n instanceof CSSStyleSheet?(t=>{let e="";for(const i of t.cssRules)e+=i.cssText;return Ot(e)})(n):n;/**
 * @license
 * Copyright 2017 Google LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */const{is:Mt,defineProperty:Nt,getOwnPropertyDescriptor:zt,getOwnPropertyNames:Ut,getOwnPropertySymbols:Rt,getPrototypeOf:jt}=Object,A=globalThis,mt=A.trustedTypes,Dt=mt?mt.emptyScript:"",Y=A.reactiveElementPolyfillSupport,U=(n,t)=>n,F={toAttribute(n,t){switch(t){case Boolean:n=n?Dt:null;break;case Object:case Array:n=n==null?n:JSON.stringify(n)}return n},fromAttribute(n,t){let e=n;switch(t){case Boolean:e=n!==null;break;case Number:e=n===null?null:Number(n);break;case Object:case Array:try{e=JSON.parse(n)}catch{e=null}}return e}},rt=(n,t)=>!Mt(n,t),gt={attribute:!0,type:String,converter:F,reflect:!1,useDefault:!1,hasChanged:rt};Symbol.metadata??(Symbol.metadata=Symbol("metadata")),A.litPropertyMetadata??(A.litPropertyMetadata=new WeakMap);let P=class extends HTMLElement{static addInitializer(t){this._$Ei(),(this.l??(this.l=[])).push(t)}static get observedAttributes(){return this.finalize(),this._$Eh&&[...this._$Eh.keys()]}static createProperty(t,e=gt){if(e.state&&(e.attribute=!1),this._$Ei(),this.prototype.hasOwnProperty(t)&&((e=Object.create(e)).wrapped=!0),this.elementProperties.set(t,e),!e.noAccessor){const i=Symbol(),s=this.getPropertyDescriptor(t,i,e);s!==void 0&&Nt(this.prototype,t,s)}}static getPropertyDescriptor(t,e,i){const{get:s,set:o}=zt(this.prototype,t)??{get(){return this[e]},set(r){this[e]=r}};return{get:s,set(r){const l=s==null?void 0:s.call(this);o==null||o.call(this,r),this.requestUpdate(t,l,i)},configurable:!0,enumerable:!0}}static getPropertyOptions(t){return this.elementProperties.get(t)??gt}static _$Ei(){if(this.hasOwnProperty(U("elementProperties")))return;const t=jt(this);t.finalize(),t.l!==void 0&&(this.l=[...t.l]),this.elementProperties=new Map(t.elementProperties)}static finalize(){if(this.hasOwnProperty(U("finalized")))return;if(this.finalized=!0,this._$Ei(),this.hasOwnProperty(U("properties"))){const e=this.properties,i=[...Ut(e),...Rt(e)];for(const s of i)this.createProperty(s,e[s])}const t=this[Symbol.metadata];if(t!==null){const e=litPropertyMetadata.get(t);if(e!==void 0)for(const[i,s]of e)this.elementProperties.set(i,s)}this._$Eh=new Map;for(const[e,i]of this.elementProperties){const s=this._$Eu(e,i);s!==void 0&&this._$Eh.set(s,e)}this.elementStyles=this.finalizeStyles(this.styles)}static finalizeStyles(t){const e=[];if(Array.isArray(t)){const i=new Set(t.flat(1/0).reverse());for(const s of i)e.unshift(dt(s))}else t!==void 0&&e.push(dt(t));return e}static _$Eu(t,e){const i=e.attribute;return i===!1?void 0:typeof i=="string"?i:typeof t=="string"?t.toLowerCase():void 0}constructor(){super(),this._$Ep=void 0,this.isUpdatePending=!1,this.hasUpdated=!1,this._$Em=null,this._$Ev()}_$Ev(){var t;this._$ES=new Promise(e=>this.enableUpdating=e),this._$AL=new Map,this._$E_(),this.requestUpdate(),(t=this.constructor.l)==null||t.forEach(e=>e(this))}addController(t){var e;(this._$EO??(this._$EO=new Set)).add(t),this.renderRoot!==void 0&&this.isConnected&&((e=t.hostConnected)==null||e.call(t))}removeController(t){var e;(e=this._$EO)==null||e.delete(t)}_$E_(){const t=new Map,e=this.constructor.elementProperties;for(const i of e.keys())this.hasOwnProperty(i)&&(t.set(i,this[i]),delete this[i]);t.size>0&&(this._$Ep=t)}createRenderRoot(){const t=this.shadowRoot??this.attachShadow(this.constructor.shadowRootOptions);return Tt(t,this.constructor.elementStyles),t}connectedCallback(){var t;this.renderRoot??(this.renderRoot=this.createRenderRoot()),this.enableUpdating(!0),(t=this._$EO)==null||t.forEach(e=>{var i;return(i=e.hostConnected)==null?void 0:i.call(e)})}enableUpdating(t){}disconnectedCallback(){var t;(t=this._$EO)==null||t.forEach(e=>{var i;return(i=e.hostDisconnected)==null?void 0:i.call(e)})}attributeChangedCallback(t,e,i){this._$AK(t,i)}_$ET(t,e){var o;const i=this.constructor.elementProperties.get(t),s=this.constructor._$Eu(t,i);if(s!==void 0&&i.reflect===!0){const r=(((o=i.converter)==null?void 0:o.toAttribute)!==void 0?i.converter:F).toAttribute(e,i.type);this._$Em=t,r==null?this.removeAttribute(s):this.setAttribute(s,r),this._$Em=null}}_$AK(t,e){var o,r;const i=this.constructor,s=i._$Eh.get(t);if(s!==void 0&&this._$Em!==s){const l=i.getPropertyOptions(s),a=typeof l.converter=="function"?{fromAttribute:l.converter}:((o=l.converter)==null?void 0:o.fromAttribute)!==void 0?l.converter:F;this._$Em=s;const c=a.fromAttribute(e,l.type);this[s]=c??((r=this._$Ej)==null?void 0:r.get(s))??c,this._$Em=null}}requestUpdate(t,e,i){var s;if(t!==void 0){const o=this.constructor,r=this[t];if(i??(i=o.getPropertyOptions(t)),!((i.hasChanged??rt)(r,e)||i.useDefault&&i.reflect&&r===((s=this._$Ej)==null?void 0:s.get(t))&&!this.hasAttribute(o._$Eu(t,i))))return;this.C(t,e,i)}this.isUpdatePending===!1&&(this._$ES=this._$EP())}C(t,e,{useDefault:i,reflect:s,wrapped:o},r){i&&!(this._$Ej??(this._$Ej=new Map)).has(t)&&(this._$Ej.set(t,r??e??this[t]),o!==!0||r!==void 0)||(this._$AL.has(t)||(this.hasUpdated||i||(e=void 0),this._$AL.set(t,e)),s===!0&&this._$Em!==t&&(this._$Eq??(this._$Eq=new Set)).add(t))}async _$EP(){this.isUpdatePending=!0;try{await this._$ES}catch(e){Promise.reject(e)}const t=this.scheduleUpdate();return t!=null&&await t,!this.isUpdatePending}scheduleUpdate(){return this.performUpdate()}performUpdate(){var i;if(!this.isUpdatePending)return;if(!this.hasUpdated){if(this.renderRoot??(this.renderRoot=this.createRenderRoot()),this._$Ep){for(const[o,r]of this._$Ep)this[o]=r;this._$Ep=void 0}const s=this.constructor.elementProperties;if(s.size>0)for(const[o,r]of s){const{wrapped:l}=r,a=this[o];l!==!0||this._$AL.has(o)||a===void 0||this.C(o,void 0,r,a)}}let t=!1;const e=this._$AL;try{t=this.shouldUpdate(e),t?(this.willUpdate(e),(i=this._$EO)==null||i.forEach(s=>{var o;return(o=s.hostUpdate)==null?void 0:o.call(s)}),this.update(e)):this._$EM()}catch(s){throw t=!1,this._$EM(),s}t&&this._$AE(e)}willUpdate(t){}_$AE(t){var e;(e=this._$EO)==null||e.forEach(i=>{var s;return(s=i.hostUpdated)==null?void 0:s.call(i)}),this.hasUpdated||(this.hasUpdated=!0,this.firstUpdated(t)),this.updated(t)}_$EM(){this._$AL=new Map,this.isUpdatePending=!1}get updateComplete(){return this.getUpdateComplete()}getUpdateComplete(){return this._$ES}shouldUpdate(t){return!0}update(t){this._$Eq&&(this._$Eq=this._$Eq.forEach(e=>this._$ET(e,this[e]))),this._$EM()}updated(t){}firstUpdated(t){}};P.elementStyles=[],P.shadowRootOptions={mode:"open"},P[U("elementProperties")]=new Map,P[U("finalized")]=new Map,Y==null||Y({ReactiveElement:P}),(A.reactiveElementVersions??(A.reactiveElementVersions=[])).push("2.1.1");/**
 * @license
 * Copyright 2017 Google LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */const R=globalThis,J=R.trustedTypes,ft=J?J.createPolicy("lit-html",{createHTML:n=>n}):void 0,St="$lit$",x=`lit$${Math.random().toFixed(9).slice(2)}$`,Ct="?"+x,Lt=`<${Ct}>`,k=document,j=()=>k.createComment(""),D=n=>n===null||typeof n!="object"&&typeof n!="function",at=Array.isArray,Ht=n=>at(n)||typeof(n==null?void 0:n[Symbol.iterator])=="function",tt=`[ 	
\f\r]`,z=/<(?:(!--|\/[^a-zA-Z])|(\/?[a-zA-Z][^>\s]*)|(\/?$))/g,vt=/-->/g,bt=/>/g,S=RegExp(`>|${tt}(?:([^\\s"'>=/]+)(${tt}*=${tt}*(?:[^ 	
\f\r"'\`<>=]|("|')|))|$)`,"g"),yt=/'/g,$t=/"/g,Et=/^(?:script|style|textarea|title)$/i,It=n=>(t,...e)=>({_$litType$:n,strings:t,values:e}),m=It(1),O=Symbol.for("lit-noChange"),u=Symbol.for("lit-nothing"),_t=new WeakMap,C=k.createTreeWalker(k,129);function kt(n,t){if(!at(n)||!n.hasOwnProperty("raw"))throw Error("invalid template strings array");return ft!==void 0?ft.createHTML(t):t}const Bt=(n,t)=>{const e=n.length-1,i=[];let s,o=t===2?"<svg>":t===3?"<math>":"",r=z;for(let l=0;l<e;l++){const a=n[l];let c,p,h=-1,y=0;for(;y<a.length&&(r.lastIndex=y,p=r.exec(a),p!==null);)y=r.lastIndex,r===z?p[1]==="!--"?r=vt:p[1]!==void 0?r=bt:p[2]!==void 0?(Et.test(p[2])&&(s=RegExp("</"+p[2],"g")),r=S):p[3]!==void 0&&(r=S):r===S?p[0]===">"?(r=s??z,h=-1):p[1]===void 0?h=-2:(h=r.lastIndex-p[2].length,c=p[1],r=p[3]===void 0?S:p[3]==='"'?$t:yt):r===$t||r===yt?r=S:r===vt||r===bt?r=z:(r=S,s=void 0);const w=r===S&&n[l+1].startsWith("/>")?" ":"";o+=r===z?a+Lt:h>=0?(i.push(c),a.slice(0,h)+St+a.slice(h)+x+w):a+x+(h===-2?l:w)}return[kt(n,o+(n[e]||"<?>")+(t===2?"</svg>":t===3?"</math>":"")),i]};class L{constructor({strings:t,_$litType$:e},i){let s;this.parts=[];let o=0,r=0;const l=t.length-1,a=this.parts,[c,p]=Bt(t,e);if(this.el=L.createElement(c,i),C.currentNode=this.el.content,e===2||e===3){const h=this.el.content.firstChild;h.replaceWith(...h.childNodes)}for(;(s=C.nextNode())!==null&&a.length<l;){if(s.nodeType===1){if(s.hasAttributes())for(const h of s.getAttributeNames())if(h.endsWith(St)){const y=p[r++],w=s.getAttribute(h).split(x),V=/([.?@])?(.*)/.exec(y);a.push({type:1,index:o,name:V[2],strings:w,ctor:V[1]==="."?Wt:V[1]==="?"?Vt:V[1]==="@"?Gt:Q}),s.removeAttribute(h)}else h.startsWith(x)&&(a.push({type:6,index:o}),s.removeAttribute(h));if(Et.test(s.tagName)){const h=s.textContent.split(x),y=h.length-1;if(y>0){s.textContent=J?J.emptyScript:"";for(let w=0;w<y;w++)s.append(h[w],j()),C.nextNode(),a.push({type:2,index:++o});s.append(h[y],j())}}}else if(s.nodeType===8)if(s.data===Ct)a.push({type:2,index:o});else{let h=-1;for(;(h=s.data.indexOf(x,h+1))!==-1;)a.push({type:7,index:o}),h+=x.length-1}o++}}static createElement(t,e){const i=k.createElement("template");return i.innerHTML=t,i}}function T(n,t,e=n,i){var r,l;if(t===O)return t;let s=i!==void 0?(r=e._$Co)==null?void 0:r[i]:e._$Cl;const o=D(t)?void 0:t._$litDirective$;return(s==null?void 0:s.constructor)!==o&&((l=s==null?void 0:s._$AO)==null||l.call(s,!1),o===void 0?s=void 0:(s=new o(n),s._$AT(n,e,i)),i!==void 0?(e._$Co??(e._$Co=[]))[i]=s:e._$Cl=s),s!==void 0&&(t=T(n,s._$AS(n,t.values),s,i)),t}class qt{constructor(t,e){this._$AV=[],this._$AN=void 0,this._$AD=t,this._$AM=e}get parentNode(){return this._$AM.parentNode}get _$AU(){return this._$AM._$AU}u(t){const{el:{content:e},parts:i}=this._$AD,s=((t==null?void 0:t.creationScope)??k).importNode(e,!0);C.currentNode=s;let o=C.nextNode(),r=0,l=0,a=i[0];for(;a!==void 0;){if(r===a.index){let c;a.type===2?c=new W(o,o.nextSibling,this,t):a.type===1?c=new a.ctor(o,a.name,a.strings,this,t):a.type===6&&(c=new Ft(o,this,t)),this._$AV.push(c),a=i[++l]}r!==(a==null?void 0:a.index)&&(o=C.nextNode(),r++)}return C.currentNode=k,s}p(t){let e=0;for(const i of this._$AV)i!==void 0&&(i.strings!==void 0?(i._$AI(t,i,e),e+=i.strings.length-2):i._$AI(t[e])),e++}}class W{get _$AU(){var t;return((t=this._$AM)==null?void 0:t._$AU)??this._$Cv}constructor(t,e,i,s){this.type=2,this._$AH=u,this._$AN=void 0,this._$AA=t,this._$AB=e,this._$AM=i,this.options=s,this._$Cv=(s==null?void 0:s.isConnected)??!0}get parentNode(){let t=this._$AA.parentNode;const e=this._$AM;return e!==void 0&&(t==null?void 0:t.nodeType)===11&&(t=e.parentNode),t}get startNode(){return this._$AA}get endNode(){return this._$AB}_$AI(t,e=this){t=T(this,t,e),D(t)?t===u||t==null||t===""?(this._$AH!==u&&this._$AR(),this._$AH=u):t!==this._$AH&&t!==O&&this._(t):t._$litType$!==void 0?this.$(t):t.nodeType!==void 0?this.T(t):Ht(t)?this.k(t):this._(t)}O(t){return this._$AA.parentNode.insertBefore(t,this._$AB)}T(t){this._$AH!==t&&(this._$AR(),this._$AH=this.O(t))}_(t){this._$AH!==u&&D(this._$AH)?this._$AA.nextSibling.data=t:this.T(k.createTextNode(t)),this._$AH=t}$(t){var o;const{values:e,_$litType$:i}=t,s=typeof i=="number"?this._$AC(t):(i.el===void 0&&(i.el=L.createElement(kt(i.h,i.h[0]),this.options)),i);if(((o=this._$AH)==null?void 0:o._$AD)===s)this._$AH.p(e);else{const r=new qt(s,this),l=r.u(this.options);r.p(e),this.T(l),this._$AH=r}}_$AC(t){let e=_t.get(t.strings);return e===void 0&&_t.set(t.strings,e=new L(t)),e}k(t){at(this._$AH)||(this._$AH=[],this._$AR());const e=this._$AH;let i,s=0;for(const o of t)s===e.length?e.push(i=new W(this.O(j()),this.O(j()),this,this.options)):i=e[s],i._$AI(o),s++;s<e.length&&(this._$AR(i&&i._$AB.nextSibling,s),e.length=s)}_$AR(t=this._$AA.nextSibling,e){var i;for((i=this._$AP)==null?void 0:i.call(this,!1,!0,e);t!==this._$AB;){const s=t.nextSibling;t.remove(),t=s}}setConnected(t){var e;this._$AM===void 0&&(this._$Cv=t,(e=this._$AP)==null||e.call(this,t))}}class Q{get tagName(){return this.element.tagName}get _$AU(){return this._$AM._$AU}constructor(t,e,i,s,o){this.type=1,this._$AH=u,this._$AN=void 0,this.element=t,this.name=e,this._$AM=s,this.options=o,i.length>2||i[0]!==""||i[1]!==""?(this._$AH=Array(i.length-1).fill(new String),this.strings=i):this._$AH=u}_$AI(t,e=this,i,s){const o=this.strings;let r=!1;if(o===void 0)t=T(this,t,e,0),r=!D(t)||t!==this._$AH&&t!==O,r&&(this._$AH=t);else{const l=t;let a,c;for(t=o[0],a=0;a<o.length-1;a++)c=T(this,l[i+a],e,a),c===O&&(c=this._$AH[a]),r||(r=!D(c)||c!==this._$AH[a]),c===u?t=u:t!==u&&(t+=(c??"")+o[a+1]),this._$AH[a]=c}r&&!s&&this.j(t)}j(t){t===u?this.element.removeAttribute(this.name):this.element.setAttribute(this.name,t??"")}}class Wt extends Q{constructor(){super(...arguments),this.type=3}j(t){this.element[this.name]=t===u?void 0:t}}class Vt extends Q{constructor(){super(...arguments),this.type=4}j(t){this.element.toggleAttribute(this.name,!!t&&t!==u)}}class Gt extends Q{constructor(t,e,i,s,o){super(t,e,i,s,o),this.type=5}_$AI(t,e=this){if((t=T(this,t,e,0)??u)===O)return;const i=this._$AH,s=t===u&&i!==u||t.capture!==i.capture||t.once!==i.once||t.passive!==i.passive,o=t!==u&&(i===u||s);s&&this.element.removeEventListener(this.name,this,i),o&&this.element.addEventListener(this.name,this,t),this._$AH=t}handleEvent(t){var e;typeof this._$AH=="function"?this._$AH.call(((e=this.options)==null?void 0:e.host)??this.element,t):this._$AH.handleEvent(t)}}class Ft{constructor(t,e,i){this.element=t,this.type=6,this._$AN=void 0,this._$AM=e,this.options=i}get _$AU(){return this._$AM._$AU}_$AI(t){T(this,t)}}const et=R.litHtmlPolyfillSupport;et==null||et(L,W),(R.litHtmlVersions??(R.litHtmlVersions=[])).push("3.3.1");const Jt=(n,t,e)=>{const i=(e==null?void 0:e.renderBefore)??t;let s=i._$litPart$;if(s===void 0){const o=(e==null?void 0:e.renderBefore)??null;i._$litPart$=s=new W(t.insertBefore(j(),o),o,void 0,e??{})}return s._$AI(n),s};/**
 * @license
 * Copyright 2017 Google LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */const E=globalThis;class b extends P{constructor(){super(...arguments),this.renderOptions={host:this},this._$Do=void 0}createRenderRoot(){var e;const t=super.createRenderRoot();return(e=this.renderOptions).renderBefore??(e.renderBefore=t.firstChild),t}update(t){const e=this.render();this.hasUpdated||(this.renderOptions.isConnected=this.isConnected),super.update(t),this._$Do=Jt(e,this.renderRoot,this.renderOptions)}connectedCallback(){var t;super.connectedCallback(),(t=this._$Do)==null||t.setConnected(!0)}disconnectedCallback(){var t;super.disconnectedCallback(),(t=this._$Do)==null||t.setConnected(!1)}render(){return O}}var xt;b._$litElement$=!0,b.finalized=!0,(xt=E.litElementHydrateSupport)==null||xt.call(E,{LitElement:b});const st=E.litElementPolyfillSupport;st==null||st({LitElement:b});(E.litElementVersions??(E.litElementVersions=[])).push("4.2.1");/**
 * @license
 * Copyright 2017 Google LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */const _=n=>(t,e)=>{e!==void 0?e.addInitializer(()=>{customElements.define(n,t)}):customElements.define(n,t)};/**
 * @license
 * Copyright 2017 Google LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */const Kt={attribute:!0,type:String,converter:F,reflect:!1,hasChanged:rt},Qt=(n=Kt,t,e)=>{const{kind:i,metadata:s}=e;let o=globalThis.litPropertyMetadata.get(s);if(o===void 0&&globalThis.litPropertyMetadata.set(s,o=new Map),i==="setter"&&((n=Object.create(n)).wrapped=!0),o.set(e.name,n),i==="accessor"){const{name:r}=e;return{set(l){const a=t.get.call(this);t.set.call(this,l),this.requestUpdate(r,a,n)},init(l){return l!==void 0&&this.C(r,void 0,n,l),l}}}if(i==="setter"){const{name:r}=e;return function(l){const a=this[r];t.call(this,l),this.requestUpdate(r,a,n)}}throw Error("Unsupported decorator location: "+i)};function f(n){return(t,e)=>typeof e=="object"?Qt(n,t,e):((i,s,o)=>{const r=s.hasOwnProperty(o);return s.constructor.createProperty(o,i),r?Object.getOwnPropertyDescriptor(s,o):void 0})(n,t,e)}/**
 * @license
 * Copyright 2017 Google LLC
 * SPDX-License-Identifier: BSD-3-Clause
 */function g(n){return f({...n,state:!0,attribute:!1})}class Xt{constructor(){this.listeners=new Map,this.connectionListeners=[],this.isConnected=!1,this.connect()}connect(){const e=`${window.location.protocol==="https:"?"wss:":"ws:"}//${window.location.host}/socket.io`;this.ws=new WebSocket(e,"socketio"),this.ws.onopen=()=>{console.log("WebSocket connected"),this.isConnected=!0,this.notifyConnectionChange(!0),this.emit("query",null)},this.ws.onmessage=i=>{try{const s=i.data;if(typeof s=="string"&&s.startsWith("2")){const o=s.substring(1),r=JSON.parse(o),l=r[0],a=r[1];console.log("Socket.IO event:",l,a);const c=this.listeners.get(l);c&&c.forEach(p=>p(a))}}catch(s){console.error("Failed to parse Socket.IO message:",s,i.data)}},this.ws.onerror=i=>{console.error("WebSocket error:",i)},this.ws.onclose=()=>{console.log("WebSocket disconnected"),this.isConnected=!1,this.notifyConnectionChange(!1)}}notifyConnectionChange(t){this.connectionListeners.forEach(e=>e(t))}onConnectionChange(t){this.connectionListeners.push(t),t(this.isConnected)}reconnect(){this.ws.readyState!==WebSocket.OPEN&&(console.log("Attempting to reconnect..."),this.ws&&this.ws.close(),this.connect())}on(t,e){this.listeners.has(t)||this.listeners.set(t,[]),this.listeners.get(t).push(e)}emit(t,e){if(this.ws.readyState===WebSocket.OPEN){const i="2"+JSON.stringify([t,e]);this.ws.send(i)}}disconnect(){this.ws.close()}}var Zt=Object.defineProperty,Yt=Object.getOwnPropertyDescriptor,lt=(n,t,e,i)=>{for(var s=i>1?void 0:i?Yt(t,e):t,o=n.length-1,r;o>=0;o--)(r=n[o])&&(s=(i?r(t,e,s):r(s))||s);return i&&s&&Zt(t,e,s),s};let H=class extends b{constructor(){super(...arguments),this.src="",this.alt="Album Art"}render(){return this.src?m`<img src="${this.src}" alt="${this.alt}">`:m`<div class="disc-icon">
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
  `;lt([f()],H.prototype,"src",2);lt([f()],H.prototype,"alt",2);H=lt([_("album-art")],H);var te=Object.defineProperty,ee=Object.getOwnPropertyDescriptor,ct=(n,t,e,i)=>{for(var s=i>1?void 0:i?ee(t,e):t,o=n.length-1,r;o>=0;o--)(r=n[o])&&(s=(i?r(t,e,s):r(s))||s);return i&&s&&te(t,e,s),s};let I=class extends b{constructor(){super(...arguments),this.current=0,this.total=0}formatTime(n){const t=Math.floor(n/60),e=n%60;return`${t}:${e.toString().padStart(2,"0")}`}get progress(){return this.total>0?this.current/this.total*100:0}render(){return m`
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
  `;ct([f({type:Number})],I.prototype,"current",2);ct([f({type:Number})],I.prototype,"total",2);I=ct([_("progress-bar")],I);var se=Object.defineProperty,ne=Object.getOwnPropertyDescriptor,Pt=(n,t,e,i)=>{for(var s=i>1?void 0:i?ne(t,e):t,o=n.length-1,r;o>=0;o--)(r=n[o])&&(s=(i?r(t,e,s):r(s))||s);return i&&s&&se(t,e,s),s};let K=class extends b{constructor(){super(...arguments),this.playing=!1}handlePlay(){this.dispatchEvent(new CustomEvent("play"))}handleNext(){this.dispatchEvent(new CustomEvent("next"))}render(){return m`
      <button @click=${this.handlePlay} class="primary" title="${this.playing?"Pause":"Play"}">
        <span class="material-icons">${this.playing?"pause":"play_arrow"}</span>
      </button>
      <button @click=${this.handleNext} title="Next Song">
        <span class="material-icons">skip_next</span>
      </button>
    `}};K.styles=$`
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
  `;Pt([f({type:Boolean})],K.prototype,"playing",2);K=Pt([_("playback-controls")],K);var ie=Object.defineProperty,oe=Object.getOwnPropertyDescriptor,ht=(n,t,e,i)=>{for(var s=i>1?void 0:i?oe(t,e):t,o=n.length-1,r;o>=0;o--)(r=n[o])&&(s=(i?r(t,e,s):r(s))||s);return i&&s&&ie(t,e,s),s};function wt(n,t=10){if(n<=50){const e=n/50;return-40*Math.pow(1-e,2)}else{const e=(n-50)/50;return t*e}}function re(n,t=10){return n<=0?Math.sqrt(1-n/-40)*50:50+n/t*50}let B=class extends b{constructor(){super(...arguments),this.volume=50,this.maxGain=10}handleVolumeChange(n){const t=n.target,e=parseInt(t.value);this.volume=e;const i=Math.round(wt(e,this.maxGain));this.dispatchEvent(new CustomEvent("volume-change",{detail:{percent:e,db:i}}))}updateFromDb(n){this.volume=Math.round(re(n,this.maxGain))}render(){const n=Math.round(wt(this.volume,this.maxGain)),t=n>=0?`+${n}`:`${n}`;return m`
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
  `;ht([f({type:Number})],B.prototype,"volume",2);ht([f({type:Number})],B.prototype,"maxGain",2);B=ht([_("volume-control")],B);var ae=Object.getOwnPropertyDescriptor,le=(n,t,e,i)=>{for(var s=i>1?void 0:i?ae(t,e):t,o=n.length-1,r;o>=0;o--)(r=n[o])&&(s=r(s)||s);return s};let nt=class extends b{handleReconnect(){this.dispatchEvent(new CustomEvent("reconnect"))}render(){return m`
      <button @click=${this.handleReconnect} title="Reconnect">
        <span class="material-icons">sync</span>
      </button>
    `}};nt.styles=$`
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
  `;nt=le([_("reconnect-button")],nt);var ce=Object.defineProperty,he=Object.getOwnPropertyDescriptor,pt=(n,t,e,i)=>{for(var s=i>1?void 0:i?he(t,e):t,o=n.length-1,r;o>=0;o--)(r=n[o])&&(s=(i?r(t,e,s):r(s))||s);return i&&s&&ce(t,e,s),s};let q=class extends b{constructor(){super(...arguments),this.rating=0,this.menuOpen=!1,this.handleClickOutside=n=>{this.menuOpen&&!n.composedPath().includes(this)&&(this.menuOpen=!1)}}connectedCallback(){super.connectedCallback(),document.addEventListener("click",this.handleClickOutside)}disconnectedCallback(){super.disconnectedCallback(),document.removeEventListener("click",this.handleClickOutside)}toggleMenu(n){n&&n.stopPropagation(),this.menuOpen=!this.menuOpen}closeMenu(){this.menuOpen=!1}handleLove(){this.dispatchEvent(new CustomEvent("love")),this.closeMenu()}handleBan(){this.dispatchEvent(new CustomEvent("ban")),this.closeMenu()}handleTired(){this.dispatchEvent(new CustomEvent("tired")),this.closeMenu()}render(){return m`
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
  `;pt([f({type:Number})],q.prototype,"rating",2);pt([g()],q.prototype,"menuOpen",2);q=pt([_("song-actions-menu")],q);var pe=Object.defineProperty,ue=Object.getOwnPropertyDescriptor,X=(n,t,e,i)=>{for(var s=i>1?void 0:i?ue(t,e):t,o=n.length-1,r;o>=0;o--)(r=n[o])&&(s=(i?r(t,e,s):r(s))||s);return i&&s&&pe(t,e,s),s};let M=class extends b{constructor(){super(...arguments),this.stations=[],this.currentStation="",this.menuOpen=!1,this.handleClickOutside=n=>{this.menuOpen&&!n.composedPath().includes(this)&&(this.menuOpen=!1)}}connectedCallback(){super.connectedCallback(),document.addEventListener("click",this.handleClickOutside)}disconnectedCallback(){super.disconnectedCallback(),document.removeEventListener("click",this.handleClickOutside)}toggleMenu(n){n&&n.stopPropagation(),this.menuOpen=!this.menuOpen}closeMenu(){this.menuOpen=!1}handleStationClick(n){this.dispatchEvent(new CustomEvent("station-change",{detail:{station:n.id}})),this.closeMenu()}render(){return m`
      <div class="menu-popup ${this.menuOpen?"":"hidden"}">
        ${this.stations.map(n=>{const t=n.isQuickMix?"shuffle":"play_arrow",e=n.isQuickMixed&&!n.isQuickMix;return m`
            <button 
              class="station-button ${n.name===this.currentStation?"active":""}"
              @click=${()=>this.handleStationClick(n)}
            >
              <span class="material-icons station-icon-leading">${t}</span>
              <span class="station-name">${n.name}</span>
              ${e?m`<span class="material-icons quickmix-icon">shuffle</span>`:""}
            </button>
          `})}
      </div>
    `}};M.styles=$`
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
  `;X([f({type:Array})],M.prototype,"stations",2);X([f()],M.prototype,"currentStation",2);X([g()],M.prototype,"menuOpen",2);M=X([_("stations-popup")],M);var de=Object.defineProperty,me=Object.getOwnPropertyDescriptor,Z=(n,t,e,i)=>{for(var s=i>1?void 0:i?me(t,e):t,o=n.length-1,r;o>=0;o--)(r=n[o])&&(s=(i?r(t,e,s):r(s))||s);return i&&s&&de(t,e,s),s};let N=class extends b{constructor(){super(...arguments),this.stations=[],this.currentStation="",this.rating=0}toggleTools(n){const t=this.shadowRoot.querySelector("stations-popup");t&&t.closeMenu();const e=this.shadowRoot.querySelector("song-actions-menu");e&&e.toggleMenu(n)}toggleStations(n){const t=this.shadowRoot.querySelector("song-actions-menu");t&&t.closeMenu();const e=this.shadowRoot.querySelector("stations-popup");e&&e.toggleMenu(n)}handleLove(){this.dispatchEvent(new CustomEvent("love"))}handleBan(){this.dispatchEvent(new CustomEvent("ban"))}handleTired(){this.dispatchEvent(new CustomEvent("tired"))}handleStationChange(n){this.dispatchEvent(new CustomEvent("station-change",{detail:n.detail}))}render(){return m`
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
  `;Z([f({type:Array})],N.prototype,"stations",2);Z([f()],N.prototype,"currentStation",2);Z([f({type:Number})],N.prototype,"rating",2);N=Z([_("bottom-toolbar")],N);var ge=Object.defineProperty,fe=Object.getOwnPropertyDescriptor,v=(n,t,e,i)=>{for(var s=i>1?void 0:i?fe(t,e):t,o=n.length-1,r;o>=0;o--)(r=n[o])&&(s=(i?r(t,e,s):r(s))||s);return i&&s&&ge(t,e,s),s};let d=class extends b{constructor(){super(...arguments),this.socket=new Xt,this.connected=!1,this.albumArt="",this.songTitle="Not Playing",this.artistName="—",this.playing=!1,this.currentTime=0,this.totalTime=0,this.volume=0,this.maxGain=10,this.rating=0,this.stations=[],this.currentStation="",this.songStationName=""}connectedCallback(){super.connectedCallback(),this.setupSocketListeners(),this.setupConnectionListener()}setupConnectionListener(){this.socket.onConnectionChange(n=>{this.connected=n,n||(this.albumArt="",this.playing=!1,this.currentTime=0,this.totalTime=0)})}setupSocketListeners(){this.socket.on("start",n=>{this.albumArt=n.coverArt,this.songTitle=n.title,this.artistName=n.artist,this.totalTime=n.duration,this.playing=!0,this.rating=n.rating||0,this.songStationName=n.songStationName||""}),this.socket.on("stop",()=>{console.log("Received stop event"),this.playing=!1,this.currentTime=0,this.totalTime=0}),this.socket.on("progress",n=>{this.currentTime=n.elapsed,this.totalTime=n.duration}),this.socket.on("stations",n=>{this.stations=Array.isArray(n)?n:[]}),this.socket.on("process",n=>{var e;console.log("Received process event:",n),n.song?(this.albumArt=n.song.coverArt||"",this.songTitle=n.song.title||"Not Playing",this.artistName=n.song.artist||"—",this.totalTime=n.song.duration||0,this.playing=n.playing||!1,this.rating=n.song.rating||0,this.songStationName=n.song.songStationName||""):(this.albumArt="",this.songTitle="Not Playing",this.artistName="—",this.playing=!1,this.currentTime=0,this.totalTime=0,this.rating=0,this.songStationName=""),n.station&&(this.currentStation=n.station),n.volume!==void 0&&(this.volume=n.volume),n.maxGain!==void 0&&(this.maxGain=n.maxGain);const t=(e=this.shadowRoot)==null?void 0:e.querySelector("volume-control");t&&n.volume!==void 0&&t.updateFromDb(n.volume)})}handlePlayPause(){this.playing=!this.playing,this.socket.emit("action","playback.toggle")}handleNext(){this.socket.emit("action","playback.next")}handleLove(){this.socket.emit("action","song.love")}handleBan(){this.socket.emit("action","song.ban")}handleTired(){this.socket.emit("action","song.tired")}handleStationChange(n){const{station:t}=n.detail;this.socket.emit("station.change",t)}handleVolumeChange(n){const{percent:t,db:e}=n.detail;this.volume=e,this.socket.emit("action",{action:"volume.set",volume:t})}handleReconnect(){this.socket.reconnect()}render(){return m`
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
  `;v([g()],d.prototype,"connected",2);v([g()],d.prototype,"albumArt",2);v([g()],d.prototype,"songTitle",2);v([g()],d.prototype,"artistName",2);v([g()],d.prototype,"playing",2);v([g()],d.prototype,"currentTime",2);v([g()],d.prototype,"totalTime",2);v([g()],d.prototype,"volume",2);v([g()],d.prototype,"maxGain",2);v([g()],d.prototype,"rating",2);v([g()],d.prototype,"stations",2);v([g()],d.prototype,"currentStation",2);v([g()],d.prototype,"songStationName",2);d=v([_("pianobar-app")],d);
