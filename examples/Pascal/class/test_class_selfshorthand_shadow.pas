program TestClassSelfshorthandShadow;
type
    TBox = class
        value: integer;
        function Bump(value: integer): integer;
    end;
var
    b: TBox;

function TBox.Bump;
begin
    { Bump's own parameter is ALSO named 'value' - inside its body, a
      bare 'value' must resolve to the PARAMETER (standard shadowing,
      matching how a local already shadows a global of the same name),
      never to the field. }
    Bump := value + 1;
end;

begin
    new(b);
    b.value := 10;

    { The returned result must be the ARGUMENT plus 1, not the field's
      own value plus 1, and the field itself must be untouched. }
    writeln('Bump(5) = ', b.Bump(5));
    writeln('field value unchanged: ', b.value);

    dispose(b);
end.
