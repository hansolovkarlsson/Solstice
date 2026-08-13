program TestDeclorderUnitInterleaved;

{ Exercises DeclOrderUnit.pas, which interleaves const/type/var (and
  splits a self-referential pointer/record pair across two 'type'
  blocks) independently in both its interface and its implementation.
  MakeNode(4) = 4 + IfaceConst(1) = 5. Bump adds ImplConst(2) to
  counter, called twice: counter=4. }

uses DeclOrderUnit;

var
    n: PNode;

begin
    n := MakeNode(4);
    writeln(n^.data);
    dispose(n);

    counter := 0;
    Bump;
    Bump;
    writeln(counter);
end.
