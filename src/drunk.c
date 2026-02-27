/***************************************************************************
 *  Original Diku Mud copyright (C) 1990, 1991 by Sebastian Hammer,        *
 *  Michael Seifert, Hans Henrik St{rfeldt, Tom Madsen, and Katja Nyboe.   *
 *                                                                         *
 *  Merc Diku Mud improvments copyright (C) 1992, 1993 by Michael          *
 *  Chastain, Michael Quan, and Mitchell Tse.                              *
 *                                                                         *
 *  In order to use any part of this Merc Diku Mud, you must comply with   *
 *  both the original Diku license in 'license.doc' as well the Merc       *
 *  license in 'license.txt'.  In particular, you may not remove either of *
 *  these copyright notices.                                               *
 *                                                                         *
 *  Much time and thought has gone into this software and you are          *
 *  benefitting.  We hope that you share your changes too.  What goes      *
 *  around, comes around.                                                  *
 ***************************************************************************/
/***************************************************************************
*	ROM 2.4 is copyright 1993-1996 Russ Taylor			   *
*	ROM has been brought to you by the ROM consortium		   *
*	    Russ Taylor (rtaylor@efn.org)				   *
*	    Gabrielle Taylor						   *
*	    Brian Moore (zump@rom.org)					   *
*	By using this code, you have agreed to follow the terms of the	   *
*	ROM license, in the file Delicate02/doc/rom.license                *
***************************************************************************/

#include <sys/types.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "merc.h"


/* How to make a string look drunk... by Apex (robink@htsa.hva.nl) */
/* Modified and enhanced for envy(2) by the Maniac from Mythran */
/* Further mods/upgrades for ROM 2.4 by Kohl Desenee */

char * makedrunk (char *string, CHAR_DATA * ch)
{
    /* This structure defines all changes for a character */
    struct struckdrunk drunk[] =
    {
	{3, 10,
	    {"а", "а", "а", "А", "аа", "ах", "Ах", "ао", "ав", "оа", "ахххх"} },
	{8, 5,
	    {"б", "б", "б", "Б", "Б", "вб"}},
	{3, 5,
	    {"в", "в", "В", "ви", "ав", "св"}},
	{3, 5,
	    {"г", "г", "Ггг", "гы", "гав", "гага"}},
	{5, 2,
	    {"д", "д", "Д"}},
	{3, 3,
	    {"е", "е", "ех", "Е"}},
/*    {3, 3,
     {"ё", "ёё", "ёмое", "ёЁЕ"}}, */
	{4, 5,
	    {"ж", "ж", "жж", "жжж", "жЖж", "Ж"}},
	{8, 4,
	    {"з", "з", "З", "ззззз", "ззз"}},
	{9, 6,
	    {"и", "и", "ии", "иии", "Иии", "ИиИ", "И"}},
	{3, 3,
	    {"й", "йй", "йЙй", "ии"}},
	{7, 6,
	    {"к", "к", "Ккк", "кк", "кК", "Кк", "К"}},
	{9, 5,
	    {"л", "л", "лл", "Лл", "лЛ", "ля"}},
	{7, 5,
	    {"м", "м", "М", "ммм", "ммэ", "мама"}},
	{3, 5,
	    {"н", "н", "Н", "ннн", "Нн", "нну"}},
	{5, 8,
	    {"о", "о", "оо", "ооо", "оооо", "ооооо", "оуу", "ое", "О"}},
	{6, 6,
	    {"п", "п", "пп", "Пп", "ппп", "пП", "пы"}},
	{3, 6,
	    {"р", "р", "ррр", "ар", "аРрр", "Ррр", "ррРр"}},
	{3, 5,
	    {"с", "с", "С", "ссс", "тссс", "сС"}},
	{5, 5,
	    {"т", "т", "Т", "ты", "туту", "Тыта"}},
	{4, 2,
	    {"у", "уууу", "Ууу"}},
	{2, 5,
	    {"ф", "фф", "ффФФ", "Фсф", "фых", "фФ"}},
	{5, 2,
	    {"х", "хы", "Х"}},
	{3, 6,
	    {"ц", "ц", "цых", "ца", "ЦццЦ", "цухх", "уц"}},
	{4, 3,
	    {"ч", "ч", "Ч", "чччч"}},
	{4, 3,
	    {"ш", "ш", "Щ", "шшшш"}},
	{4, 3,
	    {"щ", "щ", "Щ", "шшшш"}},
	{4, 3,
	    {"ъ", "ъ", "ЬЬ", "ъъ"}},
	{4, 3,
	    {"ы", "ыыыы", "Ыыы", "ыау"}},
	{4, 3,
	    {"ь", "ь", "ЪЪ", "ьь"}},
	{5, 6,
	    {"э", "э", "Э", "ээээ", "эЭэу", "эээЭ", "Экээ"}},
	{3, 3,
	    {"ю", "ю", "Ю", "Юю"}},
	{2, 9,
	    {"я", "я", "яяяяя", "яЯЯ", "Яя", "ия", "йа", "ийа", "Яауу", "уйЯ"}}
    };

    static char buf[5 * MAX_INPUT_LENGTH];
    char temp;
    int pos = 0;
    int drunklevel;
    int randomnum;

    /* Check how drunk a person is... */
    if (IS_NPC(ch))
	drunklevel = 0;
    else
        drunklevel = ch->pcdata->condition[COND_DRUNK];

    if (drunklevel > 0)
    {
    	do
        {
      	    temp = UPPER(*string);
	    if (temp >= 'А' && temp <= 'Я')
            {
	  	if (drunklevel > drunk[temp - 'А'].min_drunk_level)
                {
	    	    randomnum = number_range(0,
					     drunk[temp - 'А'].number_of_rep);
	      	    strcpy(&buf[pos], drunk[temp - 'А'].replacement[randomnum]);
		    pos += strlen (drunk[temp - 'А'].replacement[randomnum]);
                }
		else
		    buf[pos++] = *string;
            }
	    else
            {
		if ((temp >= '0') && (temp <= '9'))
                {
		    temp = '0' + number_range (0, 9);
		    buf[pos++] = temp;
                }
		else
		    buf[pos++] = *string;
            }
        }
	while (*string++)
	    ;
	    
	buf[pos] = '\0';          /* Mark end of the string... */
	return buf;
    }
    return string;
}

/* charset=cp1251 */
