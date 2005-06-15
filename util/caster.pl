#!/usr/bin/perl -w

use strict;
use warnings;

use Carp;

if($#ARGV != 1) {
    print STDERR "$0 <infile> <outfile>\n";
    exit 1;
}

my $infile=shift;
my $outfile=shift;

open(I,$infile) || croak "$infile: $!";
open(O,">$outfile") || croak "$outfile: $!";

print O "/* Generated from $infile by $0, do not edit. */\n\n";

my $lineno=0;
while(my $line=<I>) {
    chomp $line;
    ++$lineno;

    my($to,$from)=$line =~ /^\s*(.+?)\s*->\s*(.+?)\s*$/;

    my($ttype,$tname,$targs)=parse($to);
    my($ftype,$fname,$fargs)=parse($from);
    print O "\n";
    print O "#line $lineno \"$infile\"\n";
    print O "$from;\n";
    print O "#define $tname(";
    print_list($targs,1);
    print O ") (($ttype(*)(";
    print_list($targs,0);
    print O "))$fname)(";
    print_list($fargs,1);
    print O ")\n";
}

sub parse {
    my $func=shift;

    my($p1,$args)=$func =~ /^(.+?)\s*\((.+)\)$/;
    my($ftype,$fname)=parse_decl($p1);
#    print "func=$func p1=$p1 ftype=$ftype fname=$fname args=$args\n";
    my @args=split /,/,$args;
    my @alist;
    foreach my $arg (@args) {
	my($atype,$aname)=parse_decl($arg);
	print "atype=$atype aname=$aname\n";
	push @alist,[$atype,$aname];
    }
    return ($ftype,$fname,\@alist);
}

sub parse_decl {
    my $decl=reverse shift;

    my($n,$t)=$decl =~ /^([A-Za-z0-9_]+)(.+)$/;
#    print "decl=$decl n=$n t=$t\n";

    $n=reverse $n;
    $t=reverse $t;
    return ($t,$n);
}

sub print_list {
    my $list=shift;
    my $member=shift;

    my $first=1;
    foreach my $arg (@$list) {
	print O ',' if !$first;
	print O $arg->[$member];
	$first=undef;
    }
}