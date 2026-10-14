#ifndef WPARTED_I18N_H
#define WPARTED_I18N_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <glibmm/i18n.h>
#include <libintl.h>

#ifndef _
#define _(String) (String)
#endif

#ifndef N_
#define N_(String) (String)
#endif

#ifndef Q_
#define Q_(String) (String)
#endif

#ifndef C_
#define C_(Context,String) (String)
#endif

#endif /* WPARTED_I18N_H */
