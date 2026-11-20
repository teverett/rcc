program Main;

var
  sum, i: integer;

procedure DoubleIt(var x: integer);
begin
  x := x * 2;
end;

begin
  writeln('RCC PASCAL ON VEX!');
  sum := 0;
  for i := 1 to 10 do
    sum := sum + i;
  writeln('SUM 1 TO 10 =');
  writeln(sum);
  DoubleIt(sum);
  writeln('DOUBLED =');
  writeln(sum);
end.
