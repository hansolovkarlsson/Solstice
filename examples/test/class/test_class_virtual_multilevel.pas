program TestClassVirtualMultilevel;
type
    TA = class
        function Describe: integer;
    end;
    TB = class(TA)
        function Describe: integer;   { overrides TA's own }
    end;
    TC = class(TB)
        { inherits TB's Describe - never overrides it again }
    end;
var
    a: TA;
    obj: TC;

function TA.Describe;
begin
    Describe := 1;
end;

function TB.Describe;
begin
    Describe := 2;
end;

begin
    new(obj);
    { obj is TC, which never overrides Describe itself - dispatch must
      still reach TB's override (the nearest ancestor that DOES define
      it), not TA's original, and the slot number must be the SAME one
      resolved two levels down as it is directly on TB. }
    writeln('via TC instance, direct: ', obj.Describe);

    a := obj;   { upcast three levels: TC instance, TA-typed reference }
    writeln('via TA ref, TC instance: ', a.Describe);

    dispose(obj);
end.
