program TestDynarrayNilParam;

{ Passing the literal nil as a dynamic-array-typed value/var argument -
  confirms nil_compatible()'s single extension point covers argument-
  passing too, not just plain assignment. value_ok var_ok }

var
    a: array of integer;

procedure CheckValue(arr: array of integer);
begin
    if arr = nil then writeln('value_ok') else writeln('value_bad');
end;

procedure MakeNil(var arr: array of integer);
begin
    arr := nil;
end;

begin
    CheckValue(nil);
    SetLength(a, 3);
    MakeNil(a);
    if a = nil then writeln('var_ok') else writeln('var_bad');
end.
