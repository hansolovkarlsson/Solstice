program TestDynarrayNilBothOrders;

{ 'nil = arr' and 'arr = nil' both compile and agree - confirms the
  comparison isn't accidentally order-dependent.
  order1=true
  order2=true }

var
    arr: array of integer;

begin
    if arr = nil then writeln('order1=true') else writeln('order1=false');
    if nil = arr then writeln('order2=true') else writeln('order2=false');
end.
