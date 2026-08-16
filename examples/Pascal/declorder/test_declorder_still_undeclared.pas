program TestDeclorderStillUndeclared;

{ 'const'/'type' interleaving makes ordering position-based, not
  ordering-eliminated: an array typed constant's bound referencing a
  'const' declared LATER still fails - a plain declare-before-use
  error, same as any other undeclared identifier. Must NOT silently
  become "any order goes." (N is declared further down, after Zero
  tries to use it as an array bound.) }

const
    Zero: array[1..N] of integer = (0, 0, 0);

const
    N = 3;

begin
end.
