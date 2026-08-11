program TestDefaultsOverride;
{ every instance method in this compiler is already always virtually
  dispatched (no 'virtual'/'override' keyword - see docs/LANGUAGE.md).
  An override may declare its OWN, different default from the base
  method - which default applies is resolved STATICALLY, against
  whichever type the call site's own expression is declared as, exactly
  like C++/Java default arguments - even though the method BODY that
  actually runs is still chosen dynamically. }
type
    TBase = class
        procedure Greet(n: integer = 1);
    end;
    TChild = class(TBase)
        procedure Greet(n: integer = 9);
    end;
var b: TBase;
    c: TChild;

procedure TBase.Greet;
begin
    writeln('base ', n);
end;

procedure TChild.Greet;
begin
    writeln('child ', n);
end;

begin
    new(c);
    c.Greet;   { statically TChild -> default 9, runs TChild's body: "child 9" }
    b := c;
    b.Greet;   { statically TBase -> default 1, but dispatches to TChild's body: "child 1" }
    dispose(c);
end.
