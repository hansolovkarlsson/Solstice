program TestLambdaBadParamType;

{ A lambda's declared parameter/return type doesn't match the target
  procedural type's signature - confirms the EXISTING
  proc_signatures_match() error still fires correctly for a
  lambda-produced target_proc_idx. TCmp expects (integer, integer):
  boolean; this lambda takes (real, real): boolean instead. }

type
    TCmp = function(a, b: integer): boolean;

var
    cmp: TCmp;

begin
    cmp := function(a, b: real): boolean begin exit(a < b); end;
end.
