/* 
 OpenMQTTGateway - ESP8266 or Arduino program for home automation 

 Act as a gateway between your 433mhz, infrared IR, BLE, LoRa signal and one interface like an MQTT broker  
 Send and receiving command by MQTT
 
 This files enables to set your parameter for the DHT11/22 sensor
 
 Copyright: (c)Florian ROBERT
 
 This file is part of OpenMQTTGateway.
 
 OpenMQTTGateway is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenMQTTGateway is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef config_WebContent_h
#define config_WebContent_h

/*------------------- ----------------------*/

// TODO: Create a script to generate these from WebPack

#define body_footer_main_menu   "<div id=but2d></div><p><form id=but2 action='.' method='get'><button class='secondary'>Back to main menu</button></form></p>"
#define body_footer_config_menu "<div id=but3d></div><p><form id=but3 action='cn' method='get'><button class='secondary'>Back to configuration</button></form></p>"
#define body_header             "<body><main class='page'><header class='page-header'><noscript>Enable JavaScript to use this interface.<br></noscript><div class='module-list'>%s</div><h1>%s</h1></header>"

#if defined(ESP32) && defined(MQTT_HTTPS_FW_UPDATE)
#  define button_upgrade "<p><form id=but5 style='display: block;' action='up' method='get'><button>Firmware Upgrade</button></form></p>"
#else
#  define button_upgrade ""
#endif
// Configuration Menu

#define configure_1 "<p><form action='wi' method='post'><button>WiFi network</button></form></p>"
#define configure_2 "<p><form action='mq' method='post'><button>MQTT and recovery</button></form></p>"
/*#if defined(ZgatewayCloud)
#  define configure_3 "<p><form action='cl' method='get'><button>Configure Cloud</button></form></p>"
#else
#  define configure_3
#endif*/
#ifndef ESPWifiManualSetup
#  define configure_3 "<p><form action='cg' method='post'><button>Gateway security</button></form></p>"
#else
#  define configure_3
#endif
#define configure_4 "<p><form action='wu' method='get'><button>Web interface</button></form></p>"
#define configure_5 "<p><form action='lo' method='get'><button>Logging</button></form></p>"
#ifdef ZgatewayLORA
#  define configure_6 "<p><form action='la' method='get'><button>LoRa radio</button></form></p>"
#elif defined(ZgatewayRTL_433) || defined(ZgatewayPilight) || defined(ZgatewayRF) || defined(ZgatewayRF2) || defined(ZactuatorSomfy)
#  define configure_6 "<p><form action='rf' method='get'><button>RF receiver</button></form></p>"
#else
#  define configure_6
#endif
#if defined(ZsensorGPIOInput) && defined(GPIO_INPUT_RUNTIME_CONFIG)
#  define configure_7 "<p><form action='gi' method='get'><button>GPIO input sensors</button></form></p>"
#else
#  define configure_7
#endif
#define configure_8

/*------------------- ----------------------*/

const char header_html[] = "<!DOCTYPE html><html lang=\"en\" class= \" \"><head><meta charset='utf-8'><meta name= \"viewport \" content= \"width=device-width,initial-scale=1,user-scalable=no \"/><title>%s</title><script> var x = null, lt, to, tp, pc = ''; function eb(s) { return document.getElementById(s); } function qs(s) { return document.querySelector(s); } function sp(i) { eb(i).type = (eb(i).type === 'text' ? 'password' : 'text'); } function wl(f) { window.addEventListener('load', f); }";

const char root_script[] = "var ft; function la(p) {a = p || '';clearTimeout(ft);clearTimeout(lt);if (x != null) { x.abort()}x = new XMLHttpRequest();x.onreadystatechange = function() { if (x.readyState == 4 && x.status == 200) {var s = x.responseText.replace(/{t}/g,\"<table style='width:100%'> \").replace(/{s}/g,\"<tr><th> \").replace(/{m}/g,\"</th><td style='width:20px;white-space:nowrap'> \").replace(/{e}/g,\"</td></tr> \");eb('l1').innerHTML = s;clearTimeout(ft);clearTimeout(lt);lt = setTimeout(la, 2345); }};x.open('GET', '.?m=1' + a, true);x.send();ft = setTimeout(la, 20000); } function lc(v, i, p) {if (eb('s')) { if (v == 'h' || v == 'd') {var sl = eb('sl4').value;eb('s').style.background = 'linear-gradient(to right,rgb(' + sl + '%,' + sl + '%,' + sl + '%),hsl(' + eb('sl2').value + ',100%%,50%%))'; }}la('&' + v + i + '=' + p); } wl(la);";

const char restart_script[] = "setTimeout(function() { location.href = '.'; }, 15000);";

const char information_script[] = "function i() { var s, o = \"<table style='width:100%%'><tr><th>%s</td></tr></table>\"; s = o.replace(/}1/g, \"</td></tr><tr><th>\").replace(/}2/g, \"</th><td>\"); eb('i').innerHTML = s; } wl(i);";

const char console_script[] = "var sn = 0, id = 0, ft, ltm = 2345; function l(p) { var c, o = ''; clearTimeout(lt); clearTimeout(ft); t = eb('t1'); if (p == 1) { c = eb('c1'); o = '&c1=' + encodeURIComponent(c.value); c.value = ''; t.scrollTop = 99999; sn = t.scrollTop; } if (t.scrollTop >= sn) { if (x != null) { x.abort(); } x = new XMLHttpRequest(); x.onreadystatechange = function() { if (x.readyState == 4 && x.status == 200) { var z, d; d = x.responseText.split(/}1/); id = d.shift(); if (d.shift() == 0) { t.value = ''; } z = d.shift(); if (z.length > 0) { t.value += z; } t.scrollTop = 99999; sn = t.scrollTop; clearTimeout(ft); lt = setTimeout(l, ltm); } }; x.open('GET', 'cs?c2=' + id + o, true); x.send(); ft = setTimeout(l, 20000); } else { lt = setTimeout(l, ltm); } return false; } wl(l); var hc = [], cn = 0; function h() { eb('c1').addEventListener('keydown', function(e) { var b = eb('c1'), c = e.keyCode; if (38 == c || 40 == c) { b.autocomplete = 'off'; } 38 == c ? (++cn > hc.length && (cn = hc.length), b.value = hc[cn - 1] || '') : 40 == c ? (0 > --cn && (cn = 0), b.value = hc[cn - 1] || '') : 13 == c && (hc.length > 19 && hc.pop(), hc.unshift(b.value), cn = 0) }); } wl(h);";

const char wifi_script[] = "function c(l) {eb('s1').value = l.innerText || l.textContent; eb('p1').focus(); }";

const char script[] = "function jd() { var t = 0, i = document.querySelectorAll('input,button,textarea,select'); while (i.length >= t) { if (i[t]) { i[t]['name'] = (i[t].hasAttribute('id') && (!i[t].hasAttribute('name'))) ? i[t]['id'] : i[t]['name']; } t++; } } wl(jd); </script>";

const char style[] = "<style>:root{--bg:#eef3f7;--surface:#fff;--ink:#17212b;--muted:#657383;--line:#d8e1e8;--brand:#1677a8;--brand2:#0b526f;--ok:#26834a;--warn:#b96312;--shadow:0 8px 28px rgba(25,55,75,.11)}*{box-sizing:border-box}body{margin:0;text-align:center;font-family:system-ui,-apple-system,Segoe UI,sans-serif;background:linear-gradient(145deg,#eaf4f8 0,#f5f7f9 48%,#e9eef3 100%);color:var(--ink);min-height:100vh}.page{width:min(94vw,760px);display:inline-block;text-align:left;padding:24px 0 40px}.page-header{text-align:center;padding:12px 10px 18px}.page-header h1{font-size:clamp(1.45rem,5vw,2.05rem);margin:.2rem 0;letter-spacing:-.025em}.module-list{font-size:.76rem;text-transform:uppercase;letter-spacing:.08em;color:var(--muted);overflow-wrap:anywhere}fieldset{border:0;border-radius:16px;padding:18px;background:#253441;color:#edf5f8;box-shadow:var(--shadow)}fieldset.set1{background:var(--surface);color:var(--ink)}legend{padding:0 8px;font-size:1.15rem;color:inherit}legend span{position:static}p{margin:.7em 0}input,select,textarea{font:inherit;border:1px solid var(--line);border-radius:9px;padding:10px 11px;background:#f8fafb;color:var(--ink);width:100%;transition:border-color .15s,box-shadow .15s}input:focus,select:focus,textarea:focus{outline:0;border-color:#42a6d2;box-shadow:0 0 0 3px rgba(22,119,168,.14)}input[type=checkbox],input[type=radio]{width:1.1em;height:1.1em;margin-right:8px;accent-color:var(--brand);vertical-align:-2px}input[type=range]{width:99%}textarea{resize:vertical;height:318px;background:#16232d;color:#ffbd55}small,.field-hint{display:block;color:var(--muted);font-size:.78rem;line-height:1.35;margin:3px 0 7px}button{border:0;border-radius:10px;background:linear-gradient(135deg,var(--brand),var(--brand2));color:#fff;min-height:46px;padding:7px 16px;font-size:1.02rem;font-weight:650;width:100%;transition:transform .15s,box-shadow .15s,filter .15s;cursor:pointer;box-shadow:0 4px 12px rgba(11,82,111,.2)}button:hover{filter:brightness(1.08);box-shadow:0 7px 18px rgba(11,82,111,.25)}button:active{transform:translateY(1px)}button.secondary{background:#fff;color:var(--brand2);border:1px solid var(--line);box-shadow:none}.bred{background:linear-gradient(135deg,#da7a1d,#a94620)}.bgrn{background:linear-gradient(135deg,#3b9b5c,#216d3d)}a{color:var(--brand);text-decoration:none;font-weight:600}.info-box{background:#eaf6fb;border:1px solid #c4e3ef;border-radius:11px;padding:13px 14px;color:#234b5e;line-height:1.45;margin:2px 0 14px}.channel-card{border:1px solid var(--line);border-radius:14px;padding:15px;margin:14px 0;background:linear-gradient(180deg,#fff,#f9fbfc)}.channel-title{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:10px;font-size:1.06rem}.status-chip{font-size:.7rem;text-transform:uppercase;letter-spacing:.04em;border-radius:999px;padding:5px 8px;white-space:nowrap;background:#e4e9ed;color:#596672}.status-chip.active{background:#d9f2e2;color:#176b36}.status-chip.idle{background:#e5f0f7;color:#285d78}.toggle-row{display:flex;align-items:center;border-radius:9px;background:#f0f4f7;padding:10px 12px;font-weight:650}.form-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:4px 14px}.input-unit{display:flex}.input-unit input{border-radius:9px 0 0 9px}.input-unit span{display:flex;align-items:center;padding:0 11px;background:#e8eef2;border:1px solid var(--line);border-left:0;border-radius:0 9px 9px 0;color:var(--muted)}td{padding:2px 4px}.p{float:left;text-align:left}.q{float:right;text-align:right}.r{border-radius:.3em;padding:2px;margin:6px 2px}@media(max-width:600px){.page{width:95vw;padding-top:12px}.form-grid{grid-template-columns:1fr}.channel-title{align-items:flex-start;flex-direction:column}.status-chip{align-self:flex-start}fieldset{padding:14px}}</style></head>";

const char root_body[] = body_header "<fieldset><div style='padding:0;height:7.5em;margin-left:15%%;white-space:pre' id='l1' name='l1'></div></fieldset><div id=but3d></div><p><form id=but3 action='cn' method='get'><button>Device configuration</button></form></p><p><form id=but4 action='in' method='get'><button>System information</button></form></p>" button_upgrade "<p><form id=but14 action='cs' method='get'><button>Live console</button></form></p><p><form id=but0 action='.' method='get' onsubmit='return confirm(\"Confirm Restart\");'><button name='rst' class='button bred'>Restart gateway</button></form></p>";

const char config_body[] = body_header "" configure_1 "" configure_2 "" configure_3 "" configure_4 "" configure_5 "" configure_6 "" configure_7 "" configure_8 "<div id=but1d style='display: block;'></div><p><form id=but1 style='display: block;' action='rt' method='get' onsubmit='return confirm(\"Confirm Reset Configuration\");'><button name='non' class='button bred'>Reset Configuration</button></form>" body_footer_main_menu;

const char reset_body[] = body_header "<div style='text-align:center;'>%s</div><br><div style='text-align:center;'>Device will restart in a few seconds</div><br>" body_footer_main_menu;
const char config_saved_body[] = body_header "<div style='text-align:center;'>%s</div><br>" body_footer_config_menu;

//const char config_cloud_body[] = body_header "<fieldset class=\"set1\"><legend><span><b>&nbsp;Cloud Configuration&nbsp;</b></span></legend><form method='get' action='cl'><p><label><input id='cl-en' type='checkbox' %s><b>Enable Cloud Connection</b></label></p><br><p><label><input id='cl-lk' type='checkbox' disabled><b>Cloud Account%s Linked</b></label></p><br><button name='save' type='submit' class='button bgrn'>Save</button></form></fieldset><p><form action='%s' method='get'><input type='hidden' name='macAddress' value='%s'/><input type='hidden' name='redirect_uri' value='%s'/><input type='hidden' name='gateway_name' value='%s'/><input type='hidden' name='uptime' value='%d'/><input type='hidden' name='RT' value='%d'/><button>Link Cloud Account</button></form></p>" body_footer_config_menu;

const char token_body[] = body_header "<div style='text-align:center;'>Link Cloud Account</div><br><div style='text-align:center;'>Cloud was successfully linked</div><br><div id=but2d style=\"display: block;\"></div><p><form id=but2 style=\"display: block;\" action='cn' method='get'><button>Configuration</button></form></p>";

const char console_body[] = body_header "<br><textarea readonly id='t1' cols='340' wrap='off'></textarea><br><br><form method='get' onsubmit='return l(1);'><input id='c1' placeholder='Enter topic and command' autofocus><br></form>" body_footer_main_menu;

const char information_body[] = body_header "<style>td {padding: 0px 5px;}</style><div id='i' name='i'></div>" body_footer_main_menu;

const char upgrade_body[] = body_header "<div id='f1' style='display:block;'><fieldset class=\"set1\"><legend><span><b>Upgrade from Local File</b></span></legend><form method='post' action='up-local' enctype='multipart/form-data' onsubmit=\"document.getElementById('f1').style.display='none';document.getElementById('f2').style.display='block';\"><p><b>Firmware file (.bin)</b><br><input name='firmware' type='file' accept='.bin,application/octet-stream' required></p><p>Upload only a trusted firmware built for this board.</p><br><button type='submit' class='button bgrn'>Upload and install</button></form></fieldset><br><br><fieldset class=\"set1\"><legend><span><b>Upgrade by Web Server</b></span></legend><form method='get' action='up'><br><b>OTA URL</b><br><input id='o' placeholder=\"OTA_URL\" value=\"%s\"><br><br><button type='submit' class='button bgrn'>Start upgrade</button></form></fieldset><br><br><fieldset class=\"set1\"><legend><span><b>Upgrade to Level</b></span></legend><form method='get' action='up'><p><b>Level</b><br><select id='le'><option value='1'>Latest Release</option><option value='2'>Development</option></select></p><br><button type='submit' class='button bgrn'>Start upgrade</button></form></fieldset></div><div id='f2' style='display:none;text-align:center;'><b>Firmware upload in progress. Do not disconnect power.</b></div><div id=but2d style=\"display: block;\"></div><p>" body_footer_main_menu;

const char config_wifi_body[] = body_header "%s<br><div><a href='/wi?scan='><b>Scan for all WiFi Networks</b></a></div><br><fieldset class=\"set1\"><legend><span><b>WiFi Parameters</b></span></legend><form method='post' action='wi'><p><b>WiFi Network</b> () <br><input id='s1' name='s1' placeholder=\"Type or Select your WiFi Network\" value=\"%s\"></p><p><label><b>WiFi Password</b></label><br><input id='p1' name='p1' type='password' placeholder=\"Enter your WiFi Password\" ></p><br><button name='save' type='submit' class='button bgrn'>Save</button></form></fieldset>" body_footer_config_menu;

#if defined(MQTT_WOL_ENABLED) && !MQTT_BROKER_MODE
#  define MQTT_WOL_FORM \
    "<hr><p><b>Wake-on-LAN after MQTT failures</b></p>" \
    "<p><b>Enabled</b><br><input id='we' name='we' type='checkbox' %s></p>" \
    "<p><b>Destination MAC</b><br><input id='wm' name='wm' maxlength='17' pattern='[0-9A-Fa-f]{2}(:[0-9A-Fa-f]{2}){5}' value='%s'></p>" \
    "<p><b>Initial delay (seconds)</b><br><input id='wd' name='wd' type='number' min='0' max='86400' value='%lu'></p>" \
    "<p><b>Minimum consecutive failures</b><br><input id='wf' name='wf' type='number' min='1' max='1000' value='%u'></p>" \
    "<p><b>Repeat interval (seconds, 0 = once)</b><br><input id='wr' name='wr' type='number' min='0' max='86400' value='%lu'></p>" \
    "<p><b>Trigger on transport/TLS error</b><br><input id='wt' name='wt' type='checkbox' %s></p>" \
    "<p><b>Trigger on broker rejection</b><br><input id='wb' name='wb' type='checkbox' %s></p>" \
    "<p><b>Trigger on authentication error</b><br><input id='wa' name='wa' type='checkbox' %s></p>"
#else
#  define MQTT_WOL_FORM ""
#endif

#ifdef ZmqttDiscovery
// mqtt server (mh), mqtt port (ml), mqtt username (mu), mqtt password (mp), secure connection (sc), server certificate (msc), mqtt topic (mt), discovery prefix (dp)
const char config_mqtt_body[] = body_header "<fieldset class=\"set1\"><legend><span><b>MQTT Parameters</b></span></legend><form method='post' action='mq'><p><b>MQTT Server</b><br><input id='mh' name='mh' placeholder=" MQTT_SERVER " value='%s'></p><p><b>MQTT Port</b><br><input id='ml' name='ml' placeholder=" MQTT_PORT " value='%s'></p><p><b>MQTT Username</b><br><input id='mu' name='mu' placeholder=" MQTT_USER " value='%s'></p><p><label><b>MQTT Password</b></label><br><input id='mp' name='mp' type='password' placeholder=\"Leave empty to keep saved password\" maxlength='64'></p><p><b>MQTT Secure Connection</b><br><input id='sc' name='sc' type='checkbox' %s></p><p><b>Gateway Name</b><br><input id='h' name='h' placeholder=" Gateway_Name " value=\"%s\"></p><p><b>MQTT Base Topic</b><br><input id='mt' name='mt' placeholder='' value='%s'></p><p><b>MQTT Discovery Prefix</b><br><input id='dp' name='dp' placeholder='' value='%s'></p>" MQTT_WOL_FORM "<br><button name='save' type='submit' class='button bgrn'>Save</button></form></fieldset>" body_footer_config_menu;
#else
// mqtt server (mh), mqtt port (ml), mqtt username (mu), mqtt password (mp), secure connection (sc), server certificate (msc), mqtt topic (mt)
const char config_mqtt_body[] = body_header "<fieldset class=\"set1\"><legend><span><b>MQTT Parameters</b></span></legend><form method='post' action='mq'><p><b>MQTT Server</b><br><input id='mh' name='mh' placeholder=" MQTT_SERVER " value='%s'></p><p><b>MQTT Port</b><br><input id='ml' name='ml' placeholder=" MQTT_PORT " value='%s'></p><p><b>MQTT Username</b><br><input id='mu' name='mu' placeholder=" MQTT_USER " value='%s'></p><p><label><b>MQTT Password</b></label><br><input id='mp' name='mp' type='password' placeholder=\"Leave empty to keep saved password\" maxlength='64'></p><p><b>MQTT Secure Connection</b><br><input id='sc' name='sc' type='checkbox' %s></p><p><b>Gateway Name</b><br><input id='h' name='h' placeholder=" Gateway_Name " value=\"%s\"></p><p><b>MQTT Base Topic</b><br><input id='mt' name='mt' placeholder='' value='%s'></p>" MQTT_WOL_FORM "<br><button name='save' type='submit' class='button bgrn'>Save</button></form></fieldset>" body_footer_config_menu;
#endif
#if defined(MQTT_WOL_ENABLED) && !MQTT_BROKER_MODE
static_assert(sizeof(config_mqtt_body) + 512 <= WEB_TEMPLATE_BUFFER_MAX_SIZE, "MQTT/WOL WebUI template buffer is too small");
#endif
#ifndef ESPWifiManualSetup
const char config_gateway_body[] = body_header "<fieldset class=\"set1\"><legend><span><b>Gateway Configuration</b></span></legend><form method='post' action='cg'><p><b>Gateway Password (8 characters min)</b><br><input id='gp' name='gp' type='password' placeholder=\"********\"  minlength='8'></p><br><button name='save' type='submit' class='button bgrn'>Save</button></form></fieldset>" body_footer_config_menu;
#endif
const char config_logging_body[] = body_header "<fieldset class=\"set1\"><legend><span><b>OpenMQTTGateway Logging</b></span></legend><form method='get' action='lo'><p><b>Log Level</b><br><select id='lo'><option %s value='0'>Silent</option><option %s value='1'>Fatal</option><option %s value='2'>Error</option><option %s value='3'>Warning</option><option %s value='4'>Notice</option><option %s value='5'>Trace</option><option %s value='6'>Verbose</option></select></p><br><button name='save' type='submit' class='button bgrn'>Save</button></form></fieldset>" body_footer_config_menu;

const char config_webui_body[] = body_header "<fieldset class=\"set1\"><legend><span><b>Configure WebUI</b></span></legend><form method='get' action='wu'><p><b>Display Metric</b><br><input id='dm' type='checkbox' %s></p><p><b>Secure WebUI</b><br><input id='sw' type='checkbox' %s></p><br><button name='save' type='submit' class='button bgrn'>Save</button></form></fieldset>" body_footer_config_menu;

const char config_rf_body[] = body_header
    "<fieldset class=\"set1\">"
    "<legend><span><b>Configure RF</b></span></legend>"
    "<form method='get' action='rf'>"

    "<p><b>Frequency</b><br>"
    "<input type='number' id='rf' name='rf' step='any' value='%.3f'></p>"

    // Active library dropdown
    "<p><b>Active library</b><br>"
    "<select id='ar' name='ar'>%s</select></p>"

    /* // Need testing
    "<p><b>OOK Threshold</b><br>"
    "<input type='number' id='oo' name='oo' step='any' value='%d'></p>"

    "<p><b>RSSI Threshold</b><br>"
    "<input type='number' id='rs' name='rs' step='any' value='%d'></p>"
*/
    "<br><button name='save' type='submit' class='button bgrn'>Save</button>"
    "</form>"
    "</fieldset>" body_footer_config_menu;

const char config_lora_body[] = body_header
    "<fieldset class=\"set1\">"
    "<legend><span><b>Configure LORA</b></span></legend>"
    "<form method='get' action='la'>"

    "<p><b>Frequency</b><br>"
    "<select id='lf' name='lf'>"
    "<option %s value='868000000'>868MHz</option>"
    "<option %s value='915000000'>915MHz</option>"
    "<option %s value='433000000'>433MHz</option>"
    "</select></p>"

    "<p><b>TX Power</b><br>"
    "<select id='lt' name='lt'>"
    "<option %s value='0'>0 dBm</option>"
    "<option %s value='1'>1 dBm</option>"
    "<option %s value='2'>2 dBm</option>"
    "<option %s value='3'>3 dBm</option>"
    "<option %s value='4'>4 dBm</option>"
    "<option %s value='5'>5 dBm</option>"
    "<option %s value='6'>6 dBm</option>"
    "<option %s value='7'>7 dBm</option>"
    "<option %s value='8'>8 dBm</option>"
    "<option %s value='9'>9 dBm</option>"
    "<option %s value='10'>10 dBm</option>"
    "<option %s value='11'>11 dBm</option>"
    "<option %s value='12'>12 dBm</option>"
    "<option %s value='13'>13 dBm</option>"
    "<option %s value='14'>14 dBm</option>"
    "</select></p>"

    "<p><b>Spreading Factor</b><br>"
    "<select id='ls' name='ls'>"
    "<option %s value='7'>SF7</option>"
    "<option %s value='8'>SF8</option>"
    "<option %s value='9'>SF9</option>"
    "<option %s value='10'>SF10</option>"
    "<option %s value='11'>SF11</option>"
    "<option %s value='12'>SF12</option>"
    "</select></p>"

    "<p><b>Signal Bandwidth</b><br>"
    "<select id='lb' name='lb'>"
    "<option %s value='7800'>7.8 kHz</option>"
    "<option %s value='10400'>10.4 kHz</option>"
    "<option %s value='15600'>15.6 kHz</option>"
    "<option %s value='20800'>20.8 kHz</option>"
    "<option %s value='31250'>31.25 kHz</option>"
    "<option %s value='41700'>41.7 kHz</option>"
    "<option %s value='62500'>62.5 kHz</option>"
    "<option %s value='125000'>125 kHz</option>"
    "<option %s value='250000'>250 kHz</option>"
    "<option %s value='500000'>500 kHz</option>"
    "</select></p>"

    "<p><b>Coding Rate</b><br>"
    "<select id='lc' name='lc'>"
    "<option %s value='5'>4/5</option>"
    "<option %s value='6'>4/6</option>"
    "<option %s value='7'>4/7</option>"
    "<option %s value='8'>4/8</option>"
    "</select></p>"

    "<p><b>Preamble Length</b><br>"
    "<input type='number' id='ll' name='ll' value='%d'></p>"

    "<p><b>Sync Word</b><br>"
    "<input type='text' id='lw' name='lw' value='0x%02X'></p>"

    "<p><b>CRC</b><br>"
    "<input type='checkbox' id='lr' name='lr' %s></p>"

    "<p><b>Invert IQ</b><br>"
    "<input type='checkbox' id='li' name='li' %s></p>"

    "<p><b>Only known</b><br>"
    "<input type='checkbox' id='ok' name='ok' %s></p>"

    "<br><button name='save' type='submit' class='button bgrn'>Save</button>"
    "</form>"
    "</fieldset>" body_footer_config_menu;

const char footer[] = "<footer style='text-align:right;font-size:11px;color:#7c8994;padding:12px 3px'><hr style='border:0;border-top:1px solid #d8e1e8'/><a href='https://community.openmqttgateway.com' target='_blank'>%s</a></footer></main></body></html>";

// Source file - https://github.com/1technophile/OpenMQTTGateway/blob/54decb4b65c7894b926ac3a89de0c6b2a3021506/docs/.vuepress/public/favicon-16x16.png
// Workflow was, convert to ICO format using an online convertor, then use the desktop utility xxd to convert to byte array

const unsigned char Openmqttgateway_logo_mini_ico[] = {
    0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x10, 0x10, 0x00, 0x00, 0x01, 0x00,
    0x20, 0x00, 0x68, 0x04, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x28, 0x00,
    0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x01, 0x00,
    0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0xa8, 0x6a, 0x00, 0x4f, 0xa8,
    0x6a, 0x0a, 0x4f, 0xa8, 0x6a, 0x62, 0x4f, 0xa8, 0x6a, 0xb0, 0x4f, 0xa8,
    0x6a, 0xbc, 0x4f, 0xa8, 0x6a, 0xb0, 0x4f, 0xa8, 0x6a, 0x61, 0x4f, 0xa8,
    0x6a, 0x0a, 0x4f, 0xa8, 0x6a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0xa8, 0x6a, 0x00, 0x4f, 0xa8,
    0x6a, 0x03, 0x4f, 0xa8, 0x6a, 0x6a, 0x4f, 0xa8, 0x6a, 0x9a, 0x4f, 0xa8,
    0x6a, 0x41, 0x4f, 0xa8, 0x6a, 0x22, 0x4f, 0xa8, 0x6a, 0x41, 0x4f, 0xa8,
    0x6a, 0x9a, 0x4f, 0xa8, 0x6a, 0x68, 0x4f, 0xa8, 0x6a, 0x04, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0xa8,
    0x6a, 0x00, 0x4f, 0xa8, 0x6a, 0x2f, 0x4f, 0xa8, 0x6a, 0xa3, 0x4f, 0xa8,
    0x6a, 0x24, 0x4f, 0xa8, 0x6a, 0x00, 0x4f, 0xa8, 0x6a, 0x00, 0x4f, 0xa8,
    0x6a, 0x00, 0x4f, 0xa8, 0x6a, 0x25, 0x4f, 0xa8, 0x6a, 0xa2, 0x4f, 0xa8,
    0x6a, 0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0xa8,
    0x6a, 0x00, 0x4f, 0xa8, 0x6a, 0x00, 0x4f, 0xa8, 0x6a, 0x5d, 0x4f, 0xa8,
    0x6a, 0x7e, 0x4f, 0xa8, 0x6a, 0x01, 0x4f, 0xa8, 0x6a, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x4f, 0xa8, 0x6a, 0x00, 0x4f, 0xa8, 0x6a, 0x02, 0x4f, 0xa8,
    0x6a, 0x7e, 0x4f, 0xa8, 0x6a, 0x67, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x4f, 0xa8, 0x6a, 0x00, 0x4f, 0xa8, 0x6a, 0x00, 0x4f, 0xa8,
    0x6a, 0x64, 0x4f, 0xa8, 0x6a, 0x75, 0x4f, 0xa8, 0x6a, 0x00, 0x4f, 0xa8,
    0x6a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0xa8, 0x6a, 0x00, 0x4f, 0xa8,
    0x6a, 0x00, 0x4f, 0xa8, 0x6a, 0x74, 0x4f, 0xa8, 0x6a, 0x6f, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0xa8,
    0x6a, 0x00, 0x4e, 0xa8, 0x6b, 0x42, 0x4f, 0xa8, 0x6a, 0x98, 0x4f, 0xa8,
    0x6a, 0x0f, 0x4f, 0xa8, 0x6a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4f, 0xa8,
    0x6a, 0x00, 0x4f, 0xa8, 0x6a, 0x10, 0x4f, 0xa8, 0x6a, 0x98, 0x4f, 0xa8,
    0x6a, 0x48, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc6, 0x85,
    0x3d, 0x00, 0xcf, 0x82, 0x3a, 0x0c, 0xb3, 0x8b, 0x44, 0x50, 0x60, 0xa3,
    0x63, 0xa6, 0x4e, 0xa8, 0x6a, 0x72, 0x4e, 0xa8, 0x6a, 0x14, 0x4c, 0xa9,
    0x6b, 0x05, 0x4f, 0xa8, 0x6a, 0x14, 0x4f, 0xa8, 0x6a, 0x74, 0x4f, 0xa8,
    0x6a, 0x8c, 0x4f, 0xa8, 0x6a, 0x0e, 0x00, 0x99, 0xff, 0x00, 0x00, 0x99,
    0xff, 0x0b, 0x00, 0x99, 0xff, 0x25, 0x00, 0x99, 0xff, 0x1e, 0x27, 0x95,
    0xd9, 0x05, 0xc8, 0x85, 0x3b, 0x28, 0xc6, 0x85, 0x3d, 0x6e, 0xc8, 0x84,
    0x3c, 0x55, 0x70, 0x9f, 0x5e, 0x2b, 0x4e, 0xa8, 0x6a, 0x8a, 0x4f, 0xa8,
    0x6a, 0x9f, 0x4f, 0xa8, 0x6a, 0x8d, 0x4f, 0xa8, 0x6a, 0x9f, 0x4f, 0xa8,
    0x6a, 0x8a, 0x4f, 0xa8, 0x6a, 0x1f, 0x4f, 0xa8, 0x6a, 0x00, 0x00, 0x99,
    0xff, 0x19, 0x00, 0x99, 0xff, 0x63, 0x00, 0x99, 0xff, 0x60, 0x00, 0x99,
    0xff, 0x66, 0x25, 0x95, 0xda, 0x61, 0xbc, 0x86, 0x47, 0x65, 0xc8, 0x85,
    0x3b, 0x31, 0xc6, 0x85, 0x3d, 0x03, 0x73, 0x9d, 0x5c, 0x00, 0x39, 0xae,
    0x72, 0x09, 0x83, 0x98, 0x56, 0x59, 0x7c, 0x9a, 0x59, 0x76, 0x4b, 0xa9,
    0x6b, 0x31, 0x4f, 0xa8, 0x6a, 0x0a, 0x4f, 0xa8, 0x6a, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x99, 0xff, 0x5c, 0x00, 0x99, 0xff, 0x43, 0x00, 0x99,
    0xff, 0x01, 0x00, 0x99, 0xff, 0x0a, 0x06, 0x99, 0xf9, 0x66, 0x1d, 0x96,
    0xe2, 0x33, 0x00, 0x9b, 0xff, 0x00, 0xc6, 0x85, 0x3d, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xc8, 0x84, 0x3c, 0x00, 0xc8, 0x84, 0x3c, 0x4d, 0xc9, 0x84,
    0x3c, 0x44, 0xc9, 0x84, 0x3c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x99, 0xff, 0x6c, 0x00, 0x99,
    0xff, 0x26, 0x00, 0x99, 0xff, 0x00, 0x00, 0x99, 0xff, 0x00, 0x00, 0x99,
    0xff, 0x4f, 0x00, 0x99, 0xff, 0x3b, 0x00, 0x99, 0xff, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xc7, 0x85, 0x39, 0x00, 0xbc, 0x85, 0x64, 0x00, 0xc6, 0x85,
    0x3d, 0x5e, 0xc6, 0x85, 0x3d, 0x34, 0xc6, 0x85, 0x3d, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x99,
    0xff, 0x45, 0x00, 0x99, 0xff, 0x5f, 0x00, 0x99, 0xff, 0x1d, 0x00, 0x99,
    0xff, 0x2d, 0x00, 0x99, 0xff, 0x6c, 0x00, 0x99, 0xff, 0x1a, 0x00, 0x99,
    0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xbc, 0x82, 0x60, 0x00, 0xb5, 0x81,
    0x78, 0x06, 0xc3, 0x84, 0x48, 0x64, 0xc4, 0x84, 0x44, 0x23, 0xc4, 0x84,
    0x44, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x99, 0xff, 0x07, 0x00, 0x99, 0xff, 0x40, 0x00, 0x99,
    0xff, 0x64, 0x00, 0x99, 0xff, 0x60, 0x00, 0x99, 0xff, 0x29, 0x00, 0x99,
    0xff, 0x00, 0x00, 0x99, 0xff, 0x00, 0xa0, 0x7b, 0xc2, 0x00, 0xa0, 0x7b,
    0xc3, 0x0a, 0xa1, 0x7b, 0xc0, 0x42, 0xa7, 0x7d, 0xaa, 0x61, 0xa3, 0x7c,
    0xb8, 0x40, 0x9e, 0x7b, 0xc8, 0x06, 0xa0, 0x7b, 0xc2, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x99, 0xff, 0x00, 0x00, 0x99,
    0xff, 0x00, 0x00, 0x99, 0xff, 0x06, 0x00, 0x99, 0xff, 0x04, 0x00, 0x99,
    0xff, 0x00, 0x00, 0x99, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x7b,
    0xc2, 0x00, 0xa0, 0x7b, 0xc2, 0x33, 0xa0, 0x7b, 0xc2, 0x3e, 0x9d, 0x7a,
    0xce, 0x0c, 0xa0, 0x7b, 0xc3, 0x47, 0xa0, 0x7b, 0xc2, 0x27, 0xa0, 0x7b,
    0xc2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xa0, 0x7b, 0xc2, 0x00, 0xa0, 0x7b, 0xc2, 0x35, 0xa0, 0x7b,
    0xc2, 0x39, 0xa0, 0x7b, 0xc2, 0x07, 0xa0, 0x7b, 0xc2, 0x43, 0xa0, 0x7b,
    0xc2, 0x2a, 0xa0, 0x7b, 0xc2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa0, 0x7b, 0xc2, 0x00, 0xa0, 0x7b,
    0xc2, 0x0e, 0xa0, 0x7b, 0xc2, 0x52, 0xa0, 0x7b, 0xc2, 0x60, 0xa0, 0x7b,
    0xc2, 0x4b, 0xa0, 0x7b, 0xc2, 0x09, 0xa0, 0x7b, 0xc2, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0x01, 0x00, 0x00, 0xfe, 0x00,
    0x00, 0x00, 0xfe, 0x38, 0x00, 0x00, 0xfe, 0x38, 0x00, 0x00, 0xfe, 0x7c,
    0x00, 0x00, 0xfe, 0x38, 0x00, 0x00, 0xfc, 0x00, 0x00, 0x00, 0x80, 0x01,
    0x00, 0x00, 0x00, 0x83, 0x00, 0x00, 0x03, 0xcf, 0x00, 0x00, 0x33, 0xcf,
    0x00, 0x00, 0x03, 0x8f, 0x00, 0x00, 0x07, 0x07, 0x00, 0x00, 0xcf, 0x07,
    0x00, 0x00, 0xff, 0x07, 0x00, 0x00, 0xff, 0x07, 0x00, 0x00};
unsigned int Openmqttgateway_logo_mini_ico_len = 1150;

#endif
