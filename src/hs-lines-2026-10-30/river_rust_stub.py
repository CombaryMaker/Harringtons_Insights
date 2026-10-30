# Test-only stub: the Rust extension cannot be built offline. Any use raises.
import sys, types, importlib.abc, importlib.machinery
class _Missing:
    def __init__(self,*a,**k): raise RuntimeError("Rust extension not built")
    def __class_getitem__(cls,i): return cls
def _getattr(n):
    if n.startswith('__'): raise AttributeError(n)
    return _Missing
__getattr__=_getattr
class _Finder(importlib.abc.MetaPathFinder, importlib.abc.Loader):
    def find_spec(self, name, path, target=None):
        if name.startswith(__name__+'.'): return importlib.machinery.ModuleSpec(name, self, is_package=True)
    def create_module(self, spec):
        m=types.ModuleType(spec.name); m.__getattr__=_getattr; m.__path__=[]; return m
    def exec_module(self, m): pass
sys.meta_path.insert(0,_Finder())
