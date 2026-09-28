"""Local launcher text only. Never changes the game, UEVR profile or runtime."""
from pathlib import Path
import html
import json
import secrets

LANGUAGES = ('en','zh-Hans','ja','ko','es','pt-BR','fr','de','ru','ar')

def catalogs(root):
    result={}
    for code in LANGUAGES:
        value=json.loads((Path(root)/'localization/launcher'/f'{code}.json').read_text(encoding='utf-8'))
        if value.get('language')!=code or value.get('direction') not in ('ltr','rtl'):
            raise ValueError('Invalid launcher language catalog')
        result[code]=value
    return result

def language(root, data, value=None):
    available=catalogs(root)
    path=Path(data)/'language.txt'
    if value is not None:
        if not isinstance(value,str) or value not in available:
            raise ValueError('Choose a supported launcher language.')
        path.parent.mkdir(parents=True,exist_ok=True)
        temporary=path.with_name(path.name+'.'+secrets.token_hex(6)+'.tmp')
        try:
            temporary.write_text(value+'\n',encoding='ascii')
            temporary.replace(path)
        finally:
            temporary.unlink(missing_ok=True)
    try: saved=path.read_text(encoding='ascii').strip()
    except (OSError,UnicodeError): saved=''
    return {'language':saved if saved in available else '', 'catalogs':available}

def setup_html(root, code):
    available=catalogs(root)
    if code not in available: raise ValueError('Unsupported setup language')
    selected=available[code];m=selected['guide'];e=html.escape
    links=' · '.join(f'<a href="/launcher-setup/{key}" lang="{key}">{e(c["name"])}</a>' for key,c in available.items())
    steps=''.join(f'<li>{e(m[key])}</li>' for key in ('extract','runtime','start','controls'))
    notes=''.join(f'<p>{e(m[key])}</p>' for key in ('recovery','recording','scope','support'))
    return (f'<!doctype html><html lang="{code}" dir="{selected["direction"]}"><meta charset="utf-8">'
        '<meta name="viewport" content="width=device-width,initial-scale=1"><title>WuWa VR</title>'
        '<style>body{max-width:780px;margin:3rem auto;padding:0 1rem;font:17px/1.65 system-ui;background:#17191c;color:#eee}'
        'a{color:#9dcaff}li{margin:1rem 0}.risk{border-inline-start:4px solid #e9b877;padding:1rem;background:#242329}</style>'
        f'<nav>{links}</nav><h1>{e(m["title"])}</h1><p class="risk">{e(m["risk"])}</p><ol>{steps}</ol>{notes}'
        '<p><a href="/">WuWa VR Launcher</a> · <a href="https://github.com/ChronoHaxx/wuwa-vr/issues">GitHub</a></p></html>')

def setup_text(root, code):
    selected=catalogs(root)[code];m=selected['guide']
    lines=[m['title'],'',m['risk'],'']
    lines += [f'{i}. {m[key]}' for i,key in enumerate(('extract','runtime','start','controls'),1)]
    for key in ('recovery','recording','scope','support'):lines += ['',m[key]]
    return '\n'.join(lines)+'\n'
