program TestClassSelfshorthandInherited;
type
    TAnimal = class
        name: integer; { just an id/tag for this test, not a real string field }
        function Greet: integer;
    end;
    TDog = class(TAnimal)
        { TDog never redeclares 'name' or 'Greet' - both are purely
          inherited. A subclass's method body using shorthand for an
          INHERITED (not its own) member must still resolve correctly -
          current_class_ptr_idx is the subclass TDog itself, and
          class_has_member() must find 'name'/'Greet' via TDog's own
          flattened fields[]/methods[], which already include every
          ancestor's entries. }
        function Bark: integer;
    end;
var
    d: TDog;

function TAnimal.Greet;
begin
    Greet := name;
end;

function TDog.Bark;
begin
    { 'name' here is declared on TAnimal, not TDog - shorthand must
      still reach it from a TDog method body. }
    Bark := name + 1;
end;

begin
    new(d);
    d.name := 7;

    writeln('inherited Greet via shorthand base: ', d.Greet);
    writeln('subclass Bark using inherited field via shorthand: ', d.Bark);

    dispose(d);
end.
