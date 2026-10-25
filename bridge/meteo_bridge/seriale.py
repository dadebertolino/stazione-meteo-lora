"""Lettura della seriale del ricevitore, una riga alla volta.

Come il registratore: se la porta sparisce (cavo staccato, ricevitore
riavviato) riprova ogni 5 s. La porta si apre in SOLA LETTURA: l'interprete
AT del ricevitore è attivo, e qualunque cosa scritta sulla porta verrebbe
eseguita come comando.
"""
