<?php
ini_set('memory_limit', '1024M'); //Change the limit here and put it in MB, 1024M = 1GB is Set.

$array = [];

while(true) {
	$array[uniqid()] = uniqid();

	for($i=0; $i<10; $i++) {
		$array[] = $array;
	}

	echo '<pre>'.print_r($array,true).'</pre>';
}
?>