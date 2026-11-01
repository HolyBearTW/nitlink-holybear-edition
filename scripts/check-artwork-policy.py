"""Isolated artwork policy tests; Worker and official catalogue calls are mocked."""
import unittest
from unittest.mock import patch, MagicMock
import json
from pathlib import Path
from nintendo_tw_artwork import OFFICIAL, preferred_sources, playstation_url
import nintendo_tw_lineup
import preview_game_bridge as bridge

COMMUNITY={'id':1,'url':'https://cdn2.steamgriddb.com/grid/72654158f52bf25f9a8101f78aceaa2f.jpg','language':None}
WORKER={'status':'ok','data':[COMMUNITY],'page':0,'hasMore':False}

class ArtworkPolicyTests(unittest.TestCase):
    @patch.object(bridge,'fetch_json',return_value={'status':'empty','data':[],'page':0,'hasMore':False})
    def test_official_and_all_sources_reject_shared_words_and_sequel_prefixes(self,worker):
        for selected,unrelated in (('Mario Kart World','東方 New World'),('Selected Game Deluxe Edition','Another Game Deluxe Edition'),('Adventure','Adventure 2'),('Adventure Returns','Adventure')):
            good={'id':4200001998,'productTitle':selected,'url':'https://example.invalid/selected'}
            bad={'id':4200001999,'productTitle':unrelated,'url':'https://example.invalid/unrelated'}
            catalog={'status':'ok','covers':[bad,good],'page':0,'hasMore':False,'total':2,'officialProvider':'nintendo-tw'}
            for source in ('nintendo','all'):
                with self.subTest(selected=selected,source=source),patch.object(bridge.TW_LINEUP,'search',return_value=catalog),patch.object(bridge.HK_STORE,'search',side_effect=AssertionError('a matching Taiwan product already exists')):
                    result=bridge.covers(1,'all','zh-TW',0,source,selected,'switch')
                    self.assertEqual(result['artworkSources'],[good])

    def test_generic_chinese_label_repairs_english_worker_fallback(self):
        path='/games/search?q=locale-regression-fixture&lang=zh-TW'
        responses=[]
        for payload in ({'ok':True,'data':[{'id':5483943,'name':'Mario Kart World','displayName':'Mario Kart World','displayNameLanguage':'en','wikidataId':'Q131767872'}]},
                        {'entities':{'Q131767872':{'labels':{'zh':{'value':'瑪利歐賽車世界','language':'zh'}}}}}):
            response=MagicMock();response.__enter__.return_value=response;response.headers.get_content_type.return_value='application/json';response.read.return_value=json.dumps(payload).encode('utf-8');responses.append(response)
        bridge.CACHE.pop(path,None)
        with patch.object(bridge.OPENER,'open',side_effect=responses) as network:
            reply=bridge.fetch_json(path)
        self.assertEqual(reply['status'],'ok')
        self.assertEqual(reply['data'][0]['displayName'],'瑪利歐賽車世界')
        self.assertEqual(reply['data'][0]['displayNameLanguage'],'zh-TW')
        self.assertIn('www.wikidata.org/w/api.php?',network.call_args_list[1].args[0].full_url)
        bridge.CACHE.pop(path,None)

    def test_named_preview_profiles_are_bounded(self):
        self.assertEqual(bridge.library({'mode':['fixture'],'profile':['codex_account_review'],'action':['load']})['status'],'ok')
        self.assertEqual(bridge.library({'mode':['fixture'],'profile':['../private'],'action':['load']})['status'],'invalid')

    @patch.object(bridge,'fetch_json',side_effect=[{'status':'invalid','data':[]},{'status':'ok','data':[COMMUNITY],'page':0,'hasMore':False}])
    def test_legacy_worker_landscape_uses_all_shapes_for_pixel_filtering(self,worker):
        reply=bridge.covers(123,'landscape','en-US',0,'community','','other')
        self.assertEqual(len(reply['covers']),1)
        self.assertEqual(worker.call_args_list[1].args[0],'/games/123/covers?shape=all&page=0')

    @patch.object(bridge.HK_STORE,'search',side_effect=AssertionError('Hong Kong must not be queried'))
    @patch.object(bridge,'fetch_json',return_value=WORKER)
    @patch.object(bridge.TW_LINEUP,'search',side_effect=AssertionError('Worker-only covers must not query the official directory'))
    def test_worker_covers_do_not_use_static_nintendo_mappings(self,taiwan,worker,store):
        reply=bridge.covers(5267007,'all','zh-TW',0,'all','','switch')
        self.assertEqual(reply['artworkSources'],[])
        self.assertEqual([cover['provider'] for cover in reply['covers']],['steamgriddb'])
        self.assertEqual(reply['officialProvider'],'')
        store.assert_not_called()
        taiwan.assert_not_called()
        square=bridge.covers(5267007,'square','zh-TW',0,'all','','switch')
        self.assertEqual(square['artworkSources'],[])
        self.assertEqual([cover['provider'] for cover in square['covers']],['steamgriddb'])

    @patch.object(bridge.HK_STORE,'search',side_effect=AssertionError('Other platforms must not query Nintendo'))
    @patch.object(bridge,'fetch_json',return_value=WORKER)
    def test_platform_isolation(self,worker,store):
        for platform in ('playstation','other','unknown'):
            with self.subTest(platform=platform):
                reply=bridge.covers(5267007,'all','zh-TW',0,'all','Zelda',platform)
                self.assertEqual(reply['artworkSources'],[])
                self.assertEqual(len(reply['covers']),1)
        sony=bridge.covers(5454475,'all','zh-TW',0,'all','Astro Bot','playstation')
        self.assertEqual([cover['provider'] for cover in sony['artworkSources']],['playstation-tw'])
        self.assertEqual(sony['artworkSources'][0]['language'],'en-US')
        self.assertEqual(sony['artworkSources'][0]['shape'],'square')

    @patch.object(bridge,'fetch_json',return_value={'status':'ok','data':[],'page':0,'hasMore':False})
    def test_hong_kong_fallback(self,worker):
        with patch.object(bridge.TW_LINEUP,'search',return_value={'status':'empty','covers':[],'page':1,'hasMore':False,'total':0,'officialProvider':'nintendo-tw'}):
            candidate={'id':4200000999,'provider':'nintendo-hk-store','shape':'landscape','productTitle':'曠野之息'}
            with patch.object(bridge.HK_STORE,'search',return_value={'status':'ok','covers':[candidate],'page':1,'hasMore':True,'total':40}) as store:
                reply=bridge.covers(35744,'all','zh-TW',1,'nintendo','曠野之息','switch')
                self.assertEqual(reply['artworkSources'],[candidate])
                self.assertTrue(reply['hasMore'])
                self.assertEqual(reply['page'],1)
                self.assertEqual(reply['officialProvider'],'nintendo-hk-store')
                store.assert_called_once_with('曠野之息',1)

    @patch.object(bridge,'fetch_json',return_value={'status':'empty','data':[],'page':0,'hasMore':False})
    def test_all_sources_keeps_official_store_fallback_and_unknown_geometry(self,worker):
        candidate={'id':4200000998,'provider':'nintendo-hk-store','shape':'','isOfficial':True,'productTitle':'Example','url':'https://store.nintendo.com.hk/media/catalog/product/example.jpg'}
        empty={'status':'empty','covers':[],'page':0,'hasMore':False,'total':0,'officialProvider':'nintendo-tw'}
        store={'status':'ok','covers':[candidate],'page':0,'hasMore':False,'total':1}
        with patch.object(bridge.TW_LINEUP,'search',return_value=empty),patch.object(bridge.HK_STORE,'search',return_value=store):
            for provider in ('all','nintendo'):
                for shape in ('all','square','portrait','landscape'):
                    with self.subTest(provider=provider,shape=shape):
                        reply=bridge.covers(35744,shape,'zh-TW',0,provider,'Example','switch')
                        self.assertIn(candidate,reply['artworkSources'])
                        self.assertTrue(reply['artworkSources'][0]['isOfficial'])
            self.assertEqual(bridge.covers(35744,'all','zh-TW',0,'community','Example','switch')['artworkSources'],[])

    @patch.object(bridge,'fetch_json',return_value={'status':'empty','data':[],'page':0,'hasMore':False})
    def test_platform_edition_tokens_do_not_include_unrelated_games(self,worker):
        selected={'id':4200001001,'provider':'nintendo-tw','shape':'landscape','url':'https://example.invalid/selected','productTitle':'冒險傳說 王國之旅 Nintendo Switch 2 Edition'}
        unrelated={**selected,'id':4200001002,'url':'https://example.invalid/other','productTitle':'另一款遊戲 Nintendo Switch 2 Edition'}
        response={'status':'ok','covers':[selected,unrelated],'page':0,'hasMore':False,'total':2,'officialProvider':'nintendo-tw'}
        with patch.object(bridge.TW_LINEUP,'search',return_value=response) as search:
            reply=bridge.covers(1,'all','zh-TW',0,'all','冒險傳說 王國之旅 Nintendo Switch 2 Edition','switch')
            search.assert_called_once_with('冒險傳說 王國之旅',0)
            self.assertEqual(reply['artworkSources'],[selected])

    @patch.object(bridge,'fetch_json',return_value={'status':'ok','data':[],'page':0,'hasMore':False})
    def test_taiwan_official_browse_and_locale_independence(self,worker):
        candidate={'id':4200001998,'provider':'nintendo-tw','sourceLabel':'Official Nintendo Taiwan','region':'TW','isOfficial':True,'productTitle':'ZELDA無雙 災厄啟示錄',
                   'kind':'logo','shape':'landscape','url':'https://images.ctfassets.net/o5v89n4kg6h4/7001000013323143e1afaeac8b11f18660ebc15686256c/ff9b9a2c673b031deefe23e14af7e66c/b34d2cdc3942da8386bfba51c7f46fcf027f5a09901680b1c5a02d46cf585c51.jpg?w=400&fm=webp'}
        result={'status':'ok','covers':[candidate],'page':0,'hasMore':False,'total':1,'officialProvider':'nintendo-tw'}
        with patch.object(bridge.TW_LINEUP,'search',return_value=result) as taiwan, patch.object(bridge.HK_STORE,'search',side_effect=AssertionError('Taiwan results must be primary')):
            replies=[bridge.covers(5267007,'all',locale,0,'nintendo','ZELDA無雙 災厄啟示錄','switch') for locale in ('zh-TW','en-US','de-DE')]
            self.assertEqual([reply['officialProvider'] for reply in replies],['nintendo-tw']*3)
            self.assertEqual([[cover['url'] for cover in reply['artworkSources']] for reply in replies],[[candidate['url']]]*3)
            taiwan.assert_has_calls([unittest.mock.call('ZELDA無雙 災厄啟示錄',0)]*3)

    def test_taiwan_image_allowlist(self):
        valid='https://images.ctfassets.net/o5v89n4kg6h4/7001000013323143e1afaeac8b11f18660ebc15686256c/ff9b9a2c673b031deefe23e14af7e66c/b34d2cdc3942da8386bfba51c7f46fcf027f5a09901680b1c5a02d46cf585c51.jpg?w=400&fm=webp'
        self.assertTrue(bridge.TW_LINEUP.image_url(valid))
        self.assertFalse(bridge.TW_LINEUP.image_url(valid.replace('images.ctfassets.net','images.ctfassets.net.evil.test')))
        self.assertFalse(bridge.TW_LINEUP.image_url(valid.replace('w=400&fm=webp','w=800&fm=webp')))

    def test_taiwan_lineup_metadata_matches_official_key_art(self):
        image='https://images.ctfassets.net/o5v89n4kg6h4/7001000013323143e1afaeac8b11f18660ebc15686256c/ff9b9a2c673b031deefe23e14af7e66c/b34d2cdc3942da8386bfba51c7f46fcf027f5a09901680b1c5a02d46cf585c51.jpg?w=400&fm=webp'
        payload={'items':[{'region':'taiwan','title':'薩爾達傳說 王國之淚','hardwareCategory':'Nintendo Switch',
                           'imageHero':{'url':image},'pageLinkCustom':'/tw/zelda/totk/index.html'}], 'total':1}
        result=nintendo_tw_lineup.parse(json.dumps(payload).encode('utf-8'),0)
        self.assertEqual(len(result['covers']),1)
        cover=result['covers'][0]
        self.assertEqual((cover['author'],cover['language'],cover['languageVerified'],cover['kind'],cover['shape']),
                         ('Nintendo Taiwan','zh-TW',True,'keyArt','landscape'))
        self.assertEqual(cover['languageSource'],'https://www.nintendo.com/tw/zelda/totk/index.html')

    @patch.object(bridge.TW_LINEUP,'search')
    @patch.object(bridge,'fetch_json')
    def test_live_directory_match_needs_no_game_id_mapping(self,worker,taiwan):
        worker.return_value={'status':'ok','data':[{'id':5303117,'name':'Worker Game','displayName':'Worker Game'}],
                             'lookups':{},'resolvedQueries':[],'total':1}
        candidate={'id':4200001999,'provider':'nintendo-tw','sourceLabel':'Official Nintendo Taiwan','region':'TW','isOfficial':True,
                   'kind':'logo','shape':'landscape','url':'https://images.ctfassets.net/o5v89n4kg6h4/7001000013323143e1afaeac8b11f18660ebc15686256c/ff9b9a2c673b031deefe23e14af7e66c/b34d2cdc3942da8386bfba51c7f46fcf027f5a09901680b1c5a02d46cf585c51.jpg?w=400&fm=webp',
                   'productTitle':'Taiwan Product'}
        taiwan.return_value={'status':'ok','covers':[candidate]}
        reply=bridge.search('Taiwan Product','de-DE','switch')
        self.assertEqual(reply['workerCount'],1)
        self.assertEqual(reply['games'][0]['id'],5303117)
        self.assertEqual(reply['games'][0]['artworkSources'][0]['url'],candidate['url'])
        self.assertEqual(reply['games'][0]['artworkSources'][0]['provider'],'nintendo-tw')

    def test_nintendo_catalog_is_dynamic_and_playstation_seed_remains(self):
        self.assertEqual(OFFICIAL.candidates(5303117,'switch'),[])
        self.assertEqual(OFFICIAL.candidates(5267007,'switch'),[])
        self.assertEqual(OFFICIAL.merge([],'王國之淚','zh-TW','switch'),[])
        sony=OFFICIAL.merge([],'Astro Bot','zh-TW','playstation')
        self.assertEqual(sony[0]['source'],'playstation-tw')
        self.assertEqual(OFFICIAL.merge([],'Astro Bot','zh-TW','switch'),[])
        self.assertEqual(preferred_sources(sony[0]['artworkSources'],'unknown'),[])
        self.assertFalse(playstation_url('https://image.api.playstation.com.evil.test/vulcan/a.png'))

    def test_nintendo_game_search_uses_worker_without_title_expansions(self):
        expansions=json.loads((Path(__file__).resolve().parents[1]/'game-search-expansions.json').read_text(encoding='utf-8'))
        entries=[value for group in expansions.get('groups',[]) for field in ('aliases','queries','suggestions') for value in group.get(field,[])]
        self.assertFalse(any('zelda' in value.casefold() or 'hyrule' in value.casefold() for value in entries))

    def test_korean_japanese_titles_require_matching_provenance(self):
        original='The Legend of Zelda'
        self.assertEqual(bridge.localized_game_name(original,'ゼルダの伝説','ja-JP','ja'), 'ゼルダの伝説')
        self.assertEqual(bridge.localized_game_name(original,'젤다의 전설','ko-KR','ko-KR'), '젤다의 전설')
        for locale in ('ja-JP','ko-KR'):
            with self.subTest(locale=locale):
                self.assertEqual(bridge.localized_game_name(original,'薩爾達傳說',locale,'zh-hant'),original)
                self.assertEqual(bridge.localized_game_name(original,'薩爾達傳說',locale,''),original)
                self.assertEqual(bridge.localized_game_name(original,'The Legend of Zelda',locale,'en'),original)
        response={'status':'ok','data':[{'id':35744,'name':original,'displayName':'薩爾達傳說',
                    'displayNameLanguage':'zh-hant','source':'wikidata'}], 'lookups':{},'resolvedQueries':[]}
        with patch.object(bridge,'fetch_json',return_value=response):
            for locale in ('ja-JP','ko-KR'):
                result=bridge.search('test title',locale,'other')
                self.assertEqual(result['games'][0]['displayName'],original)

if __name__=='__main__':unittest.main(verbosity=2)
