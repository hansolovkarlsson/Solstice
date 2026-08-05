program TestNestedDCE;

{ Outer's own local 'hidden' is never read/written directly in Outer's
  own body - only from inside the nested procedure Reveal. Regression
  guard: dead-code elimination must not strip the 'hidden := 42;'
  assignment just because Outer's own body never references it (this
  project has hit this exact class of bug three times before, for other
  node types that introduce a variable reference - see optimizer.c's
  mark_used_variables()) }
procedure Outer;
    var hidden: integer;

    procedure Reveal;
    begin
        writeln('hidden=', hidden);
    end;

begin
    hidden := 42;
    Reveal;
end;

begin
    Outer;
end.
