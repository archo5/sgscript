<?php
define("BAILOUT",16);
define("MAX_ITERATIONS",1000);

class Mandelbrot
{
	function Mandelbrot()
	{
		$d1 = microtime(1);
		for ($y = -39; $y < 39; $y++) {
			echo("\n");
			for ($x = -39; $x < 39; $x++) {
				if ($this->iterate($x/40.0,$y/40.0) == 0) 
					echo("*");
				else
					echo(" ");

			}
		}
		$d2 = microtime(1);
		$diff = $d2 - $d1;
		printf("\nPHP Elapsed %0.2f", $diff);
	}

	function iterate($x,$y)
	{
		$cr = $y-0.5;
		$ci = $x;
		$zi = 0.0;
		$zr = 0.0;
		$i = 0;
		while (true) {
			$i++;
			$temp = $zr * $zi;
			$zr2 = $zr * $zr;
			$zi2 = $zi * $zi;
			$zr = $zr2 - $zi2 + $cr;
			$zi = $temp + $temp + $ci;
			if ($zi2 + $zr2 > BAILOUT)
				return $i;
			if ($i > MAX_ITERATIONS)
				return 0;
		}
	
	}
}

$m = new Mandelbrot();