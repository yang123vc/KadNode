
#include <sys/stat.h> 
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include "log.h"
#include "sha1.h"
#include "utils.h"


/* Fill buffer with random bytes */
int id_random( void *buffer, size_t size ) {
	int rc, fd;

	fd = open( "/dev/urandom", O_RDONLY );
	if( fd < 0 ) {
		log_err( "Failed to open /dev/urandom" );
	}

	if( (rc = read( fd, buffer, size )) >= 0 ) {
		close( fd );
	}

	return rc;
}

void id_compute( UCHAR *id, const char *str ) {
	SHA1_CTX ctx;
	size_t size;
	char *tld;

	/* Remove the top level domain */
	tld = strrchr( str, '.' );
	if( tld == NULL ) {
		size = strlen( str );
	} else {
		size = tld - str;
	}

	if( size >= HEX_LEN && str_isHex( str, HEX_LEN ) ) {
		/* treat hostname as hex string and ignore any kind of suffix */
		id_fromHex( id, str, HEX_LEN );
	} else {
		SHA1_Init( &ctx );
		SHA1_Update( &ctx, (const UCHAR *) str, size );
		SHA1_Final( &ctx, id );
	}
}


/* Parse a hexdecimal string into id */
void id_fromHex( UCHAR *id, const char *hex, size_t size ) {
	int i;
	int xv = 0;

	for( i = 0; i < size; ++i ) {
		const char c = hex[i];
		if( c >= 'a' ) {
			xv += (c - 'a') + 10;
		} else if ( c >= 'A') {
			xv += (c - 'A') + 10;
		} else {
			xv += c - '0';
		}

		if( i % 2 ) {
			id[i/2] = xv;
			xv = 0;
		} else {
			xv *= 16;
		}
	}
}

/* Check if string consist of hexdecimal characters */
int str_isHex( const char *string, int size ) {
	int i = 0;

	for( i = 0; i < size; i++ ) {
		const char c = string[i];
		if( (c >= '0' && c <= '9')
			|| (c >= 'A' && c <= 'F')
			|| (c >= 'a' && c <= 'f') ) {
			continue;
		} else {
			return 0;
		}
	}

	return 1;
}

int str_isValidHostname( const char *hostname, int size ) {
	int i;

	for( i = 0; i < size; i++ ) {
		const char c = hostname[i];
		if( (c >= '0' && c <= '9')
			|| (c >= 'A' && c <= 'Z')
			|| (c >= 'a' && c <= 'z')
			|| (c == '-')
			|| (c == '.')
			|| (c == '_') ) {
			continue;
		} else {
			return 0;
		}
	}

	return 1;
}

char* str_id( const UCHAR *in, char *buf ) {
	int i = 0;
	UCHAR *p0 = (UCHAR *)in;
	char *p1 = buf;

	memset( buf, '\0', HEX_LEN+1 );

	for( i = 0; i < SHA_DIGEST_LENGTH; i++ ) {
		snprintf( p1, 3, "%02x", *p0 );
		p0 += 1;
		p1 += 2;
	}

	return buf;
}

char* str_addr( IP *addr, char *addrbuf ) {
	char buf[INET6_ADDRSTRLEN+1];
	unsigned short port;

	switch(addr->ss_family) {
		case AF_INET6:
			port = ntohs( ((IP6 *)addr)->sin6_port );
			inet_ntop( AF_INET6, &((IP6 *)addr)->sin6_addr, buf, sizeof(buf) );
			sprintf( addrbuf, "[%s]:%d", buf, port );
			break;
		case AF_INET:
			port = ntohs( ((IP4 *)addr)->sin_port );
			inet_ntop( AF_INET, &((IP4 *)addr)->sin_addr, buf, sizeof(buf) );
			sprintf( addrbuf, "%s:%d", buf, port );
			break;
		default:
			sprintf( addrbuf, "<invalid address>" );
	}

	return addrbuf;
}

char* str_addr6( IP6 *addr, char *addrbuf ) {
	return str_addr( (IP *)addr, addrbuf );
}

char* str_addr4( IP4 *addr, char *addrbuf ) {
	return str_addr( (IP *)addr, addrbuf );
}

int addr_port( const IP *addr ) {
	switch( addr->ss_family ) {
		case AF_INET: return ((IP4 *)addr)->sin_port;
		case AF_INET6: return ((IP6 *)addr)->sin6_port;
		default: return 0;
	}
}

int addr_len( const IP *addr ) {
	switch( addr->ss_family ) {
		case AF_INET: return sizeof(IP4);
		case AF_INET6: return sizeof(IP6);
		default: return 0;
	}
}

int addr_parse( IP *addr, const char *addr_str, const char *port_str, int af ) {
	struct addrinfo hints;
	struct addrinfo *info = NULL;
	struct addrinfo *p = NULL;

	memset( &hints, '\0', sizeof(struct addrinfo) );
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_family = af;

	if( getaddrinfo( addr_str, port_str, &hints, &info ) != 0 ) {
		return 1;
	}

	p = info;
	while( p != NULL ) {
		if( p->ai_family == AF_INET6 ) {
			memcpy( addr, p->ai_addr, sizeof(IP6) );
			freeaddrinfo( info );
			return 0;
		}
		if( p->ai_family == AF_INET ) {
			memcpy( addr, p->ai_addr, sizeof(IP4) );
			freeaddrinfo( info );
			return 0;
		}
	}

	freeaddrinfo( info );
	return 1;
}

/*
* Parse various string representations of
* IPv4/IPv6 addresses and optional port.
* An address can also be a domain name.
* A port can also be a service  (e.g. 'www').
*
* "<ipv4_addr>"
* "<ipv6_addr>"
* "<ipv4_addr>:<port>"
* "[<ipv4_addr>]"
* "[<ipv6_addr>]"
* "[<ipv4_addr>]:<port>"
* "[<ipv6_addr>]:<port>"
*/
int addr_parse_full( IP *addr, const char *full_addr_str, const char* default_port, int af ) {
	char addr_buf[256];

	char *addr_beg, *addr_tmp;
	char *last_colon;
	const char *addr_str = NULL;
	const char *port_str = NULL;
	int len;

	len = strlen( full_addr_str );
	if( len >= (sizeof(addr_buf) - 1) ) {
		/* address too long */
		return 1;
	} else {
		addr_beg = addr_buf;
	}

	memset( addr_buf, '\0', sizeof(addr_buf) );
	memcpy( addr_buf, full_addr_str, len );

	last_colon = strrchr( addr_buf, ':' );

	if( addr_beg[0] == '[' ) {
		/* [<addr>] or [<addr>]:<port> */
		addr_tmp = strrchr( addr_beg, ']' );

		if( addr_tmp == NULL ) {
			/* broken format */
			return 1;
		}

		*addr_tmp = '\0';
		addr_str = addr_beg + 1;

		if( *(addr_tmp+1) == '\0' ) {
			port_str = default_port;
		} else if( *(addr_tmp+1) == ':' ) {
			port_str = addr_tmp + 2;
		} else {
			/* port expected */
			return 1;
		}
	} else if( last_colon && last_colon == strchr( addr_buf, ':' ) ) {
		/* <non-ipv6-addr>:<port> */
		addr_tmp = last_colon;
		if( addr_tmp ) {
			*addr_tmp = '\0';
			addr_str = addr_buf;
			port_str = addr_tmp+1;
		} else {
			addr_str = addr_buf;
			port_str = default_port;
		}
	} else {
		/* <addr> */
		addr_str = addr_buf;
		port_str = default_port;
	}

	return addr_parse( addr, addr_str, port_str, af );
}
