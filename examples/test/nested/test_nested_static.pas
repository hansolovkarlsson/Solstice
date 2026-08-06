program TestNestedStatic;

{ nested procedure touches an enclosing 'static' local - static locals
  are already hidden globals under the hood, so this should compile to
  ordinary global opcodes, no OP_LOAD_ENCLOSING/OP_STORE_ENCLOSING
  (verified separately via -v/desole) }
procedure Outer;
    var static callCount: integer;

    procedure Tick;
    begin
        callCount := callCount + 1;
    end;

begin
    Tick;
    Tick;
    Tick;
    writeln('callCount=', callCount);
end;

begin
    Outer;
    Outer;
end.
