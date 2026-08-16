program TestClassCtorBadNobody;
type
    TFoo = class
        x: integer;
        procedure Init;
        { Init is declared but never given a body anywhere - legal on
          its own (a class can have bodyless method headers as long as
          nothing calls them), but calling it via new(f, Init) must
          still be the existing "doesn't have a body yet" error. }
    end;
var
    f: TFoo;
begin
    new(f, Init);
end.
