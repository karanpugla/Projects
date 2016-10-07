#!/usr/bin/perl
use strict;
# calculate memory to be occupied from percentage given
sub find_memto_occupy
{
my $pc = $_[0];
die “Wrong percentage given $pc\n” if ($pc > 100);

open MEMINFO, ‘<', '/proc/meminfo' or die "Unable to open /proc/meminfo to find available memory\n";
my $mem = ;

if ( $mem =~ /^MemTotal:\s+(\d+)\s.*$/ ) {
$mem = $1;
} else {
die “Unable to find the available memory\n”;
}

$mem = ( $mem / 100 ) * $pc;
return int($mem / 1024);
}

# main script
{
my $num = $ARGV[0];
my ($pc, $mb, $memfile);
unless ( defined $num and $num =~ /^\d+%?$/ and $num >= 1) {
die “Usage: $0 \nEx: $0 100 – occupies 100 MB memory\n”
}

if ( $num =~ /^(\d+)%$/ ) {
# convert percentage to bytes.
$pc = $1;
$mb = find_memto_occupy($pc);
} else {
$mb = $num;
}

$b = $mb * 1024 * 1024;
open MEM, ‘>’, \$memfile;
seek MEM, $b – 1, 0;
print MEM chr(0);
close RAM;

print “$mb MB memory is occupied, press ENTER to release: “; ;
undef $memfile;
print “Memory released”;
}