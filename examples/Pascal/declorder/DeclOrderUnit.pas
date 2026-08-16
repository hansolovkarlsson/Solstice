unit DeclOrderUnit;

{ Interleaved const/type/var sections in BOTH the interface and the
  implementation, independently - each is its own declaration-part
  scope, with its own resolve_pending_pointer_types() call (see
  load_unit() in parser.c). The interface's PNode/TNode split mirrors
  test_declorder_pointer_forward_split.pas; the implementation adds a
  second, its own, unrelated pointer/const/type interleaving to confirm
  the implementation section works too. }

interface

type
    PNode = ^TNode;

const
    IfaceConst = 1;

type
    TNode = record
        data: integer;
        next: PNode;
    end;

var
    counter: integer;

function MakeNode(v: integer): PNode;
procedure Bump;

implementation

type
    PLocal = ^TLocal;

const
    ImplConst = 2;

type
    TLocal = record
        val: integer;
    end;

function MakeNode;
var
    n: PNode;
begin
    new(n);
    n^.data := v + IfaceConst;
    n^.next := nil;
    MakeNode := n;
end;

procedure Bump;
var
    loc: PLocal;
begin
    new(loc);
    loc^.val := ImplConst;
    counter := counter + loc^.val;
    dispose(loc);
end;

end.
