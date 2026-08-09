program TestClassSelfshorthandGlobal;
type
    TBox = class
        counter: integer;
        function ReadCounter: integer;
    end;
var
    b: TBox;
    counter: integer; { a GLOBAL variable sharing the field's own name }

function TBox.ReadCounter;
begin
    { Inside the method body, bare 'counter' must resolve to the CLASS
      FIELD (a member wins over a same-named global - the shorthand
      fallback chain checks class members before the global-var
      fallback), not the global below. }
    ReadCounter := counter;
end;

begin
    counter := 999; { the global - must stay untouched by anything below }
    new(b);
    b.counter := 42;

    writeln('via shorthand method, sees field: ', b.ReadCounter);
    writeln('global still its own value: ', counter);

    dispose(b);
end.
