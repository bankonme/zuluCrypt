/*
 *
 *  Copyright (c) 2011
 *  name : mhogo mchungu
 *  email: mhogomchungu@gmail.com
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "includes.h"
#include <locale.h>
#include <stdio.h>
#include <libintl.h>
#include <sys/stat.h>

/*
 * Its not possible to add more keys to a volume with no empty slots or to a non luks volume
 *
 * This function checks if a volume is luks and if it has atleast one empty slot.
 */
static int _zuluCryptCheckEmptySlots( const char * device )
{
	int status = 0 ;
	char * c  ;
	char * d  ;

	zuluCryptSecurityGainElevatedPrivileges() ;
	/*
	 * zuluCryptEmptySlots() is defined in ../lib/empty_slots.c
	 */
	c = zuluCryptEmptySlots( device ) ;

	zuluCryptSecurityDropElevatedPrivileges() ;

	if( c == NULL ){
		/*
		 * we got here because the volume is either not luks based or the path is invalid
		 */
		status = 1 ;
	}else{
		d = c - 1 ;
		while( *++d ){
			if( *d == '0' ){
				status = 2 ;
				break ;
			}
		}
		free( c ) ;
	}

	return status ;
}

static int zuluExit( int st,stringList_t stl )
{
	zuluCryptSecurityUnlockMemory( stl ) ;
	/*
	 * this function is defined in ../string/StringList.c
	 */
	StringListClearDelete( &stl ) ;

	switch( st ){
		case 0  : printf( gettext( "SUCCESS: key added successfully\n" ) ) ;	                              	break ;
		case 1  : printf( gettext( "ERROR: presented key does not match any key in the volume\n" ) ) ;         	break ;
		case 2  : printf( gettext( "ERROR: could not open luks volume\n" ) ) ;		                      	break ;
		case 3  : printf( gettext( "ERROR: volume is not a luks volume\n" ) );		                      	break ;
		case 4  : printf( gettext( "ERROR: insufficient privilege to open a system device,\n\
only root user or members of group \"zulucrypt\" can do that\n" ) ) ;							break ;
		case 5  : printf( gettext( "ERROR: could not open volume in write mode\n" ) ) ;                        	break ;
		case 6  : printf( gettext( "ERROR: all key slots are occupied, can not add any more keys\n" ) ) ;      	break ;
		case 7  : printf( gettext( "ERROR: can not get passphrase in silent mode\n" ) ) ;			break ;
		case 8  : printf( gettext( "ERROR: insufficient memory to hold passphrase\n" ) ) ;			break ;
		case 9  : printf( gettext( "ERROR: new passphrases do not match\n" ) ) ;                              	break ;
		case 10 : printf( gettext( "ERROR: one or more required argument(s) for this operation is missing\n" ));break ;
		case 11 : printf( gettext( "ERROR: one or both keyfile(s) does not exist\n" ) ) ;                      	break ;
		case 12 : printf( gettext( "ERROR: insufficient privilege to open key file for reading\n" ) ) ;		break ;
		case 13 : printf( gettext( "ERROR: couldnt get enought memory to hold the key file\n" ) ) ;	     	break ;
		case 14 : printf( gettext( "ERROR: could not get a key from a socket\n" ) ) ;				break ;
		case 15 : printf( gettext( "ERROR: could not get elevated privilege,check binary permissions\n" ) ) ;	break ;
		default : printf( gettext( "ERROR: unrecognized error with status number %d encountered\n" ),st ) ;
	}

	return st ;
}

static int zuluExit_1( int st,const char * device,stringList_t stl )
{
	printf( gettext( "ERROR: device \"%s\" is not a luks device\n" ),device ) ;
	StringListClearDelete( &stl ) ;
	return st ;
}

static int zuluGetKeys( string_t * key1,string_t * key2,string_t * key3 )
{
	int st ;
	/*
	 * ZULUCRYPT_KEY_MAX_SIZE is set in ../constants.h
	 */
	printf( gettext( "Enter an existing passphrase: " ) ) ;
	st = StringSilentlyGetFromTerminal_1( key1,ZULUCRYPT_KEY_MAX_SIZE ) ;
	if( st != 0 ){
		return st ;
	}
	printf( gettext( "\nEnter the new passphrase: " ) ) ;
	st = StringSilentlyGetFromTerminal_1( key2,ZULUCRYPT_KEY_MAX_SIZE ) ;
	if( st != 0 ){
		StringClearDelete( key1 ) ;
		return st ;
	}
	printf( gettext( "\nRe enter the new passphrase: " ) ) ;
	st = StringSilentlyGetFromTerminal_1( key3,ZULUCRYPT_KEY_MAX_SIZE ) ;
	if( st != 0 ){
		StringClearDelete( key1 ) ;
		StringClearDelete( key2 ) ;
		return st ;
	}

	printf( "\n" ) ;
	return 0 ;
}

/*
 * get_pass_from_file function is defined at get_pass_from_file.c *
 */
int zuluCryptEXEAddKey( const struct_opts * opts,uid_t uid )
{
	const char * device      = opts->device ;
	const char * keyType1    = opts->existing_key_source ;
	const char * existingKey = opts->existing_key ;
	const char * keyType2    = opts->new_key_source ;
	const char * newKey      = opts->new_key ;

	/*
	 * Below is a form of memory management.All strings are collected in a stringlist object to easily delete them
	 * when the function returns.This allows for the function to have multiple exit points without risks of leaking
	 * memory from manually examining each exit point to make sure all strings are deleted or go with multiple goto
	 * code deleting blocks to take into account different exit points.
	 */
	stringList_t stl ;
	string_t * stringArray  = StringListArray( &stl,5 ) ;
	string_t * presentKey	= &stringArray[ 0 ] ;
	string_t * newKey_1  	= &stringArray[ 1 ] ;
	string_t * newKey_2    	= &stringArray[ 2 ] ;
	string_t * ek          	= &stringArray[ 3 ] ;
	string_t * nk          	= &stringArray[ 4 ] ;

	const char * key1 = NULL ;
	const char * key2 = NULL ;

	size_t len1 = 0 ;
	size_t len2 = 0 ;

	int status = 0 ;

	/*
	 * zuluCryptPartitionIsSystemPartition() is defined in ./partitions.c
	 */
	if( zuluCryptPartitionIsSystemPartition( device,uid ) ){
		if( !zuluCryptUserIsAMemberOfAGroup( uid,"zulucrypt" ) ){
			return zuluExit( 4,stl ) ;
		}
	}
	/*
	 * zuluCryptSecurityDeviceIsWritable() is defined in path_access.c
	 */
	status = zuluCryptCanOpenPathForWriting( device,uid ) ;
	/*
	 * 1-permissions denied
	 * 2-invalid path
	 * 3-shenanigans
	 * 4-common error
	 */
	switch( status ){
		case 0 :  break ;
		case 1 :  return zuluExit( 5,stl ) ;
		case 2 :  return zuluExit( 5,stl ) ;
		case 3 :  return zuluExit( 5,stl ) ;
		case 4 :  return zuluExit( 5,stl ) ;
		default:  return zuluExit( 5,stl ) ;
	}

	zuluCryptSecurityGainElevatedPrivileges() ;

	/*
	 * zuluCryptVolumeIsNotLuks() is defined in ../lib/is_luks.c
	 */
	status = zuluCryptVolumeIsNotLuks( device ) ;

	zuluCryptSecurityDropElevatedPrivileges() ;

	if( status ){
		return zuluExit_1( 3,device,stl ) ;
	}

	switch( _zuluCryptCheckEmptySlots( device ) ){
		case 0 : return zuluExit( 6,stl ) ;
		case 1 : return zuluExit( 2,stl )  ;
	}

	if( keyType1 == NULL && keyType2 == NULL ){
		switch( zuluGetKeys( presentKey,newKey_1,newKey_2 ) ){
			case 1 : return zuluExit( 7,stl ) ;
			case 2 : return zuluExit( 8,stl ) ;
		}

		if( !StringEqualString( *newKey_1,*newKey_2 ) ){
			status = 9 ;
		}else{
			key1 = StringContent( *presentKey ) ;
			len1 = StringLength ( *presentKey ) ;
			key2 = StringContent( *newKey_1   ) ;
			len2 = StringLength ( *newKey_1   ) ;
		}
	}else{
		if( newKey == NULL || existingKey == NULL ){
			return zuluExit( 10,stl ) ;
		}
		if( StringsAreEqual( keyType1,"-f" ) ){
			/*
			 * this function is defined at "path_access.c"
			 */
			switch( zuluCryptGetPassFromFile( existingKey,uid,ek ) ){
				case 1 : return zuluExit( 11,stl ) ;
				case 4 : return zuluExit( 12,stl ) ;
				case 2 : return zuluExit( 13,stl ) ;
				case 5 : return zuluExit( 14,stl ) ;
			}
			key1 = StringContent( *ek ) ;
			len1 = StringLength( *ek ) ;
		}
		if( StringsAreEqual( keyType2,"-f" ) ){
			/*
			 * this function is defined at "path_access.c"
			 */
			switch( zuluCryptGetPassFromFile( newKey,uid,nk ) ){
				case 1 : return zuluExit( 11,stl ) ;
				case 4 : return zuluExit( 12,stl ) ;
				case 2 : return zuluExit( 13,stl ) ;
				case 5 : return zuluExit( 14,stl ) ;
			}
			key2 = StringContent( *nk ) ;
			len2 = StringLength( *nk ) ;
		}
		if( StringsAreEqual( keyType1,"-f" ) && StringsAreEqual( keyType2,"-f" ) ){
			;
		}else if( StringsAreEqual( keyType1,"-p" ) && StringsAreEqual( keyType2,"-p" ) ){
			key1 = existingKey ;
			len1 = StringSize( existingKey ) ;
			key2 = newKey ;
			len2 = StringSize( newKey ) ;
		}else if( StringsAreEqual( keyType1,"-p" ) && StringsAreEqual( keyType2,"-f" ) ){
			key1 = existingKey ;
			len1 = StringSize( existingKey ) ;
		}else if( StringsAreEqual( keyType1,"-f" ) && StringsAreEqual( keyType2,"-p" ) ){
			key2 = newKey ;
			len2 = StringSize( newKey ) ;
		}else{
			return zuluExit( 10,stl ) ;
		}
	}

	zuluCryptSecurityLockMemory( stl ) ;

	zuluCryptSecurityGainElevatedPrivileges() ;
	/*
	 * zuluCryptAddKey() is defined in ../lib/add_key.c
	 */
	status = zuluCryptAddKey( device,key1,len1,key2,len2 ) ;

	zuluCryptSecurityDropElevatedPrivileges() ;

	/*
	 * this function is defined in check_invalid_key.c
	 */
	zuluCryptCheckInvalidKey( device ) ;
	return zuluExit( status,stl ) ;
}
