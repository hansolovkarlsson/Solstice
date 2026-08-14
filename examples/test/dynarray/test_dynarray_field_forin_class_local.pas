program TestForInClassFieldLocal;
{ Same as test_dynarray_field_forin_class.pas, but the class-instance
  variable itself is a procedure LOCAL, not a global - exercises
  build_forin_dynarray_read()'s local heap-base path
  (heap_base_is_local). }
type
    TFoo = class
        data: array of integer;
    end;

procedure SumFoo;
var
    f: TFoo;
    x, total: integer;
begin
    new(f);
    f.data := [1, 2, 3, 4, 5];
    total := 0;
    for x in f.data do total := total + x;
    writeln(total);                 { 15 }
end;

begin
    SumFoo;
end.
