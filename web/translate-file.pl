#!/usr/bin/perl

use strict;
use warnings;
use Getopt::Std;

sub process_arguments
{
    my %opts = ();
    my $ret;

    $ret = getopts("l:i:o:", \%opts);

    if (!$ret ||
        !defined($opts{"l"}) ||
        !defined($opts{"i"}) ||
        !defined($opts{"o"}) ||
        @ARGV > 0)
    {
        print STDERR "usage: $0 -l <translation-file> " .
            "-i <input-file> -o <output-file>\n";
        exit 1;
    }

    return %opts;
}

sub cleanup_value
{
    my ($value) = @_;

    $value =~ s/^\s+//;
    $value =~ s/\s+$//;
    $value =~ s/\s+/ /g;

    return $value;
}

sub read_translation_file
{
    my ($translation_filename) = @_;
    my %trans;
    my $key;
    my $value = "";

    open(my $infile, '<', $translation_filename)
        or die("Couldn't open $translation_filename");

    while (my $line = <$infile>)
    {
        if ($line =~ /^@([^@]+)\@$/)
        {
            if (defined($key))
            {
                $trans{$key} = cleanup_value($value);
                $value = "";
            }

            $key = $1;
        }
        elsif (defined($key))
        {
            $value .= $line;
        }
    }

    if (defined($key))
    {
        $trans{$key} = cleanup_value($value);
    }

    close($infile);

    return %trans;
}

sub make_regexp
{
    return "@(" . join("|", map(quotemeta($_), @_)) . ")@";
}

sub translate_file
{
    my ($filename, $trans, $infile, $outfile) = @_;
    my $re = make_regexp(keys(%$trans));
    my $line_num = 1;

    while (my $line = <$infile>)
    {
        $line =~ s/$re/$trans->{$1}/eg;
        print $outfile $line;

        my @bad_replacements = $line =~ /@([a-z_0-9A-Z]+)@/g;

        for my $br (@bad_replacements)
        {
            print STDERR "warning: $filename:$line_num: " .
                "untranslated key: $br\n";
        }

        $line_num++;
    }
}

my %opts = process_arguments();
my %trans = read_translation_file($opts{"l"});

open(my $infile, "<", $opts{'i'})
    or die("Couldn't open $opts{'i'}");
open(my $outfile, ">", $opts{'o'})
    or die("Couldn't open $opts{'o'}");

translate_file($opts{"i"}, \%trans, $infile, $outfile);

close($infile);
close($outfile);
