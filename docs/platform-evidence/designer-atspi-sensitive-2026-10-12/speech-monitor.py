import runpy
from datetime import datetime,timezone
from pathlib import Path
from orca import debug,event_manager,focus_manager
from orca.ax_utilities import AXUtilities

speech_path=Path('/tmp/lumen-orca-designer-trace-speech.log')
trace_path=Path('/tmp/lumen-orca-designer-trace-events.log')
original=debug.print_message

def record(text):
    with trace_path.open('a',encoding='utf-8') as stream:
        stream.write(datetime.now(timezone.utc).isoformat()+' '+text+'\n')

def print_message(level,text,timestamp=False,stack=False):
    if text.startswith('SPEECH OUTPUT:'):
        with speech_path.open('a',encoding='utf-8') as stream:
            stream.write(datetime.now(timezone.utc).isoformat()+' '+text+'\n')
    return original(level,text,timestamp,stack)

debug.print_message=print_message
process=event_manager.EventManager._process_object_event

def process_event(self,event):
    record('process '+event.type+' '+str(event.detail1))
    return process(self,event)

event_manager.EventManager._process_object_event=process_event
activate=event_manager.EventManager._is_activatable_event

def activatable(self,event,script=None):
    result=activate(self,event,script)
    record('activatable '+event.type+' '+str(result))
    return result

event_manager.EventManager._is_activatable_event=activatable
active_window=AXUtilities.can_be_active_window

def can_be_active(window):
    result=active_window(window)
    record('can_be_active_window '+str(result))
    return result

AXUtilities.can_be_active_window=staticmethod(can_be_active)
set_locus=focus_manager.FocusManager.set_locus_of_focus

def set_focus(self,*args,**kwargs):
    record('set_locus_of_focus')
    return set_locus(self,*args,**kwargs)

focus_manager.FocusManager.set_locus_of_focus=set_focus
runpy.run_path('/usr/bin/orca',run_name='__main__')
