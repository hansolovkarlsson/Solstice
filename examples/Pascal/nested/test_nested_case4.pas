program TestNestedCase4;

{ Bar (nested inside Foo) is called directly from the main program,
  which is neither Foo nor a descendant of Foo - Foo is never even
  called. Under the flat-namespace design this compiles fine; it must
  trap at RUNTIME, with a clear message, the moment Bar actually
  touches Foo's own local x - not silently read garbage. }
procedure Foo;
    var x: integer;

    procedure Bar;
    begin
        x := x + 1;
        writeln('x=', x);
    end;

begin
    x := 0;
end;

begin
    Bar;
end.
