program TestInheritedNoargs;

// 'inherited MethodName;' (no parens at all) and 'inherited
// MethodName();' (empty parens) both work identically for a zero-
// argument ancestor method - parse_class_method_call_arguments()
// already tolerates a missing '(...)' as "zero arguments" for an
// ordinary call, reused unchanged here. Both print "A.Zap" once each.

type
  TA = class
    procedure Zap;
  end;
  TB = class(TA)
    procedure Zap;
  end;

var b: TB;

procedure TA.Zap;
begin
  writeln('A.Zap');
end;

procedure TB.Zap;
begin
  inherited Zap;
  inherited Zap();
  writeln('B.Zap');
end;

begin
  new(b);
  b.Zap;
  dispose(b);
end.
