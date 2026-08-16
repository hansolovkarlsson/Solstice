program TestSubrangeDCE;
type
    TAge = 0..150;
var
    a: TAge;
begin
    { 'a' is never read after this - dead-code elimination must NOT
      remove this assignment, since the subrange bounds check it carries
      is an observable runtime effect (it should abort the program). }
    a := 200;
end.
