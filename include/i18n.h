#ifndef WPARTED_I18N_H
#define WPARTED_I18N_H

	#ifdef HAVE_CONFIG_H
		#include "config.h"
	#endif /* HAVE_CONFIG_H */
	
	#ifdef ENABLE_NLS
		#include <glibmm/i18n.h>
	#else
		#define _(String) (String)
		#define N_(String) (String)
		#define Q_(String) (String)
		#define C_(Context,String) (String)
	#endif /* ENABLE_NLS */

#endif /* WPARTED_I18N_H */
