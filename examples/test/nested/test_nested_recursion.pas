program TestNestedRecursion;

{ CountDown recurses (calling itself, not Outer), while every activation
  still reaches the SAME Outer activation's own local - exercises
  vm_static_link[fp] correctness across several simultaneously-active
  recursive activations of the same nested procedure }
procedure Outer;
    var total: integer;

    procedure CountDown(n: integer);
    begin
        if n > 0 then begin
            total := total + n;
            CountDown(n - 1);
        end;
    end;

begin
    total := 0;
    CountDown(5);
    writeln('total=', total);
end;

begin
    Outer;
end.
