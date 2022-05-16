# localisation (l10n) and internationalization (i18n)
i18n = import('i18n')
add_project_arguments('-DGETTEXT_PACKAGE="fetchmail"', language:'c')
i18n.gettext(meson.project_name(), args: '--directory=' + meson.source_root())
