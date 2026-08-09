program TestSelfshorthandBadUnknown;
type
    TBox = class
        value: integer;
        function Bad: integer;
    end;
var
    b: TBox;

function TBox.Bad;
begin
    { 'nosuchname' is neither a local/param, nor a field/method of TBox,
      nor a global - must still be the ordinary "Unknown identifier"
      compile error, unchanged by the shorthand lookup added in the
      middle of this resolution chain. }
    Bad := nosuchname;
end;

begin
    new(b);
    writeln(b.Bad);
    dispose(b);
end.
